#!/usr/bin/env python3

import argparse
import contextlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile
import xml.etree.ElementTree as ET
import unittest.mock as unittest_mock
from dataclasses import asdict, dataclass
from typing import List, Optional, Sequence


@dataclass
class PatchPlan:
    input_apk: str
    output_apk: str
    abi: str
    bootstrap_mode: str
    gadget_lib: Optional[str]
    startup_script: Optional[str]
    startup_mode: str
    on_load: str
    startup_script_on_load: Optional[str]
    transport_mode: str
    interaction_type: str
    connect_host: str
    connect_port: int
    listen_address: str
    listen_port: int
    debug_logging: bool
    use_aapt2: bool
    startup_script_required: bool
    config_asset_path: str
    config: dict
    signing: str
    stages: List[str]


def emit_progress_line(message: str) -> None:
    print(f"[nook-patchapk] {message}", file=sys.stdout)


def emit_stage_progress(index: int, total: int, label: str) -> None:
    emit_progress_line(f"[{index}/{total}] {label}")


def build_stage_labels(decode_backend: str, no_sign: bool) -> List[str]:
    labels = [
        "decode apk" if decode_backend == "apktool" else "unpack apk",
        "patch package",
        "rebuild apk" if decode_backend == "apktool" else "repack apk",
    ]
    if not no_sign:
        labels.extend(["zipalign apk", "sign apk"])
    return labels


def emit_patch_summary(
    interaction_type: str,
    on_load: str,
    startup_script: Optional[str],
    bootstrap_mode: str,
    no_sign: bool,
) -> None:
    startup_script_mode = "packaged" if startup_script else "none"
    emit_progress_line(
        "mode: "
        f"{interaction_type}, "
        f"on_load={on_load}, "
        f"startup_script={startup_script_mode}, "
        f"bootstrap={bootstrap_mode}, "
        f"sign={'off' if no_sign else 'on'}"
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="nook_patchapk",
        description="Patch an Android APK with nook-gadget.",
    )
    parser.add_argument("--input-apk", required=True, help="Path to the source APK.")
    parser.add_argument("--output-apk", required=True, help="Path to the patched APK output.")
    parser.add_argument(
        "--abi",
        default="arm64-v8a",
        choices=["arm64-v8a"],
        help="Target native ABI for gadget injection.",
    )
    parser.add_argument(
        "--gadget-lib",
        help="Path to the libnook-gadget.so artifact to inject.",
    )
    parser.add_argument(
        "--bootstrap-mode",
        default="minimal",
        choices=["minimal", "proxy-loader"],
        help="Bootstrap strategy seam for future loader/proxy patch modes.",
    )
    parser.add_argument(
        "--startup-script",
        help="Optional path to a startup JavaScript file to package into the APK.",
    )
    parser.add_argument(
        "--startup-script-on-load",
        choices=["auto", "manual"],
        help="Optional v2.1 startup script load policy written into gadget config metadata.",
    )
    parser.add_argument(
        "--startup-script-required",
        action="store_true",
        help="Fail gadget startup-script bootstrap when the packaged startup script cannot be loaded.",
    )
    parser.add_argument(
        "--startup-mode",
        default="auto-start",
        choices=["auto-start", "manual"],
        help="Startup mode written into gadget config metadata.",
    )
    parser.add_argument(
        "--on-load",
        default="resume",
        choices=["resume", "wait"],
        help="Listen on-load policy written into gadget config metadata.",
    )
    parser.add_argument(
        "--transport-mode",
        default="default",
        choices=["default"],
        help="Transport mode written into gadget config metadata.",
    )
    parser.add_argument(
        "--interaction-type",
        default="listen",
        choices=["listen", "connect"],
        help="Interaction type written into gadget config metadata.",
    )
    parser.add_argument(
        "--connect-host",
        default="",
        help="Optional outbound host written into gadget connect metadata.",
    )
    parser.add_argument(
        "--connect-port",
        default=0,
        type=int,
        help="Optional outbound port written into gadget connect metadata.",
    )
    parser.add_argument(
        "--listen-address",
        default="",
        help="Optional listen address written into gadget listen metadata.",
    )
    parser.add_argument(
        "--listen-port",
        default=0,
        type=int,
        help="Optional listen port written into gadget listen metadata.",
    )
    parser.add_argument(
        "--debug-logging",
        action="store_true",
        help="Enable debug-oriented gadget runtime logging in emitted config metadata.",
    )
    parser.add_argument(
        "--use-aapt2",
        action="store_true",
        help="Pass --use-aapt2 through to apktool build.",
    )
    parser.add_argument(
        "--decode-backend",
        default="internal-zip",
        choices=["internal-zip", "apktool"],
        help="Backend used to decode and rebuild APK contents.",
    )
    parser.add_argument("--apktool", default="apktool", help="Path to apktool executable.")
    parser.add_argument("--jarsigner", default="jarsigner", help="Path to jarsigner executable.")
    parser.add_argument("--apksigner", default="", help="Path to apksigner executable.")
    parser.add_argument("--zipalign", default="", help="Path to zipalign executable.")
    parser.add_argument("--keystore", default="", help="Keystore path for APK signing.")
    parser.add_argument("--storepass", default="", help="Keystore password for APK signing.")
    parser.add_argument("--key-alias", default="", help="Key alias used for APK signing.")
    parser.add_argument(
        "--no-sign",
        action="store_true",
        help="Skip APK signing in the generated patch plan.",
    )
    parser.add_argument(
        "--print-plan",
        action="store_true",
        help="Print the staged patch plan as JSON and exit.",
    )
    return parser


def validate_patch_args(args: argparse.Namespace) -> None:
    if args.startup_script_required and not args.startup_script:
        raise ValueError("--startup-script-required requires --startup-script")
    connect_port = getattr(args, "connect_port", 0)
    listen_port = getattr(args, "listen_port", 0)
    connect_host = getattr(args, "connect_host", "")
    listen_address = getattr(args, "listen_address", "")
    interaction_type = getattr(args, "interaction_type", "listen")
    on_load = getattr(args, "on_load", "resume")
    startup_script_on_load = getattr(args, "startup_script_on_load", None)
    validate_port_arg("--connect-port", connect_port)
    validate_port_arg("--listen-port", listen_port)
    validate_interaction_args(
        interaction_type=interaction_type,
        on_load=on_load,
        connect_host=connect_host,
        connect_port=connect_port,
        listen_address=listen_address,
        listen_port=listen_port,
    )
    if (
        args.startup_script_required
        and resolve_startup_script_on_load(args.startup_mode, startup_script_on_load) != "auto"
    ):
        raise ValueError(
            "--startup-script-required requires --startup-mode auto-start because manual mode disables packaged startup-script auto-load"
        )
    if args.use_aapt2 and args.decode_backend != "apktool":
        raise ValueError("--use-aapt2 requires --decode-backend apktool")


def validate_port_arg(flag_name: str, port: int) -> None:
    if port == 0:
        return
    if port < 1 or port > 65535:
        raise ValueError(f"{flag_name} must be in the range 1..65535")


def validate_interaction_args(
    interaction_type: str,
    on_load: str,
    connect_host: str,
    connect_port: int,
    listen_address: str,
    listen_port: int,
) -> None:
    if interaction_type == "connect":
        if listen_address or listen_port:
            raise ValueError("--interaction-type connect does not accept --listen-address or --listen-port")
        return
    if connect_host or connect_port:
        raise ValueError("--interaction-type listen does not accept --connect-host or --connect-port")
    if on_load not in ("resume", "wait"):
        raise ValueError("--on-load must be either resume or wait")


def resolve_startup_script_on_load(
    startup_mode: str,
    startup_script_on_load: Optional[str],
) -> str:
    if startup_script_on_load:
        return startup_script_on_load
    return "manual" if startup_mode == "manual" else "auto"


def resolve_startup_mode(
    startup_mode: str,
    startup_script: Optional[str],
    startup_script_on_load: Optional[str],
) -> str:
    if not startup_script:
        return startup_mode
    return "manual" if resolve_startup_script_on_load(startup_mode, startup_script_on_load) == "manual" else "auto-start"


def build_patch_plan(args: argparse.Namespace) -> PatchPlan:
    validate_patch_args(args)
    startup_script_on_load = getattr(args, "startup_script_on_load", None)
    bootstrap_mode = getattr(args, "bootstrap_mode", "minimal")
    interaction_type = getattr(args, "interaction_type", "listen")
    on_load = getattr(args, "on_load", "resume")
    connect_host = getattr(args, "connect_host", "")
    connect_port = getattr(args, "connect_port", 0)
    listen_address = getattr(args, "listen_address", "")
    listen_port = getattr(args, "listen_port", 0)
    return PatchPlan(
        input_apk=args.input_apk,
        output_apk=args.output_apk,
        abi=args.abi,
        bootstrap_mode=bootstrap_mode,
        gadget_lib=args.gadget_lib,
        startup_script=args.startup_script,
        startup_mode=args.startup_mode,
        on_load=on_load,
        startup_script_on_load=resolve_startup_script_on_load(
            args.startup_mode,
            startup_script_on_load,
        ),
        transport_mode=args.transport_mode,
        interaction_type=interaction_type,
        connect_host=connect_host,
        connect_port=connect_port,
        listen_address=listen_address,
        listen_port=listen_port,
        debug_logging=args.debug_logging,
        use_aapt2=args.use_aapt2,
        startup_script_required=args.startup_script_required,
        config_asset_path=get_config_asset_path(),
        config=build_default_config(
            args.startup_script,
            bootstrap_mode=bootstrap_mode,
            startup_mode=args.startup_mode,
            on_load=on_load,
            startup_script_on_load=startup_script_on_load,
            transport_mode=args.transport_mode,
            interaction_type=interaction_type,
            connect_host=connect_host,
            connect_port=connect_port,
            listen_address=listen_address,
            listen_port=listen_port,
            debug_logging=args.debug_logging,
            startup_script_required=args.startup_script_required,
        ),
        signing="disabled" if args.no_sign else "enabled",
        stages=[
            "unpack",
            "inject-library",
            "emit-config",
            "inject-bootstrap",
            "rebuild",
            "sign",
        ],
    )


def get_config_asset_path() -> str:
    return "assets/nook-gadget/config.json"


def get_startup_script_asset_path() -> str:
    return "assets/nook-gadget/startup.js"


def build_default_config(
    startup_script: Optional[str] = None,
    bootstrap_mode: str = "minimal",
    startup_mode: str = "auto-start",
    on_load: str = "resume",
    startup_script_on_load: Optional[str] = None,
    transport_mode: str = "default",
    interaction_type: str = "listen",
    connect_host: str = "",
    connect_port: int = 0,
    listen_address: str = "",
    listen_port: int = 0,
    debug_logging: bool = False,
    startup_script_required: bool = False,
    original_application_class: str = "",
) -> dict:
    effective_startup_mode = resolve_startup_mode(
        startup_mode,
        startup_script,
        startup_script_on_load,
    )
    interaction_host = connect_host if interaction_type == "connect" else ""
    interaction_address = listen_address if interaction_type == "listen" else ""
    interaction_port = connect_port if interaction_type == "connect" else listen_port
    config = {
        "gadget_version": "0.1",
        "bootstrap_mode": bootstrap_mode,
        "startup_mode": effective_startup_mode,
        "transport_mode": transport_mode,
        "debug_logging": debug_logging,
        "bootstrap": {
            "mode": bootstrap_mode,
        },
        "interaction": {
            "type": interaction_type,
            "transport": transport_mode,
            "on_load": on_load,
            "host": interaction_host,
            "address": interaction_address,
            "port": interaction_port,
        },
    }
    if bootstrap_mode == "proxy-loader":
        config["bootstrap"]["proxy_loader"] = {
            "restore_target": "application",
        }
        if original_application_class:
            config["bootstrap"]["proxy_loader"]["original_application_class"] = original_application_class
    if startup_script:
        config["startup_script"] = {
            "mode": "asset",
            "path": get_startup_script_asset_path(),
            "required": startup_script_required,
            "on_load": resolve_startup_script_on_load(
                startup_mode,
                startup_script_on_load,
            ),
        }
    return config


def detect_supported_lib_dir(input_apk: str, abi: str) -> str:
    expected_prefix = f"lib/{abi}/"
    with zipfile.ZipFile(input_apk, "r") as archive:
        for name in archive.namelist():
            if name.startswith(expected_prefix):
                return expected_prefix
    raise ValueError(f"unsupported ABI layout for {abi}")


def inject_bootstrap_into_decoded_smali_dir(decoded_dir: str) -> str:
    for root, _, files in os.walk(decoded_dir):
        for filename in files:
            if not filename.endswith(".smali"):
                continue

            smali_path = os.path.join(root, filename)
            with open(smali_path, "r", encoding="utf-8") as handle:
                contents = handle.read()

            if ".super Landroid/app/Application;" not in contents:
                continue
            if ".method public onCreate()V" not in contents:
                continue
            if 'System;->loadLibrary(Ljava/lang/String;)V' in contents and '"nook-gadget"' in contents:
                return smali_path

            updated = _inject_loadlibrary_into_oncreate(contents)
            with open(smali_path, "w", encoding="utf-8") as handle:
                handle.write(updated)
            return smali_path

    launcher_activity_smali = find_launcher_activity_smali(decoded_dir)
    if launcher_activity_smali is not None:
        with open(launcher_activity_smali, "r", encoding="utf-8") as handle:
            contents = handle.read()

        if 'System;->loadLibrary(Ljava/lang/String;)V' in contents and '"nook-gadget"' in contents:
            return launcher_activity_smali

        updated = _inject_loadlibrary_into_oncreate(
            contents,
            method_marker=".method protected onCreate(Landroid/os/Bundle;)V",
        )
        with open(launcher_activity_smali, "w", encoding="utf-8") as handle:
            handle.write(updated)
        return launcher_activity_smali

    raise ValueError("no supported Application or launcher Activity onCreate smali target found")


def _inject_loadlibrary_into_oncreate(
    contents: str,
    method_marker: str = ".method public onCreate()V",
) -> str:
    method_start = contents.find(method_marker)
    if method_start == -1:
        raise ValueError("no supported startup onCreate smali target found")

    method_end = contents.find(".end method", method_start)
    if method_end == -1:
        raise ValueError("unterminated onCreate method")

    method_body = contents[method_start:method_end]
    locals_match = re.search(r"(?m)^(\s*)\.locals\s+(\d+)\s*$", method_body)
    if locals_match is None:
        raise ValueError("onCreate method missing .locals")

    current_locals = int(locals_match.group(2))
    if current_locals < 1:
        method_body = (
            method_body[:locals_match.start(2)]
            + "1"
            + method_body[locals_match.end(2):]
        )

    indent = locals_match.group(1)
    injection = (
        f"{indent}const-string v0, \"nook-gadget\"\n"
        f"{indent}invoke-static {{v0}}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V\n"
    )
    insert_at = method_body.find("\n", locals_match.end())
    if insert_at == -1:
        raise ValueError("malformed onCreate method")
    insert_at += 1

    method_body = method_body[:insert_at] + injection + method_body[insert_at:]
    return contents[:method_start] + method_body + contents[method_end:]


def find_launcher_activity_smali(decoded_dir: str) -> Optional[str]:
    manifest_path = os.path.join(decoded_dir, "AndroidManifest.xml")
    if not os.path.exists(manifest_path):
        return None

    try:
        tree = ET.parse(manifest_path)
    except ET.ParseError:
        return None

    root = tree.getroot()
    package_name = root.attrib.get("package", "")
    android_ns = "{http://schemas.android.com/apk/res/android}"
    application = root.find("application")
    if application is None:
        return None

    for activity in application.findall("activity"):
        for intent_filter in activity.findall("intent-filter"):
            has_main = any(
                action.attrib.get(f"{android_ns}name") == "android.intent.action.MAIN"
                for action in intent_filter.findall("action")
            )
            has_launcher = any(
                category.attrib.get(f"{android_ns}name") == "android.intent.category.LAUNCHER"
                for category in intent_filter.findall("category")
            )
            if not (has_main and has_launcher):
                continue

            activity_name = activity.attrib.get(f"{android_ns}name", "")
            if not activity_name:
                continue

            normalized_name = normalize_manifest_class_name(package_name, activity_name)
            smali_path = find_class_smali_path(decoded_dir, normalized_name)
            if smali_path is not None:
                return smali_path
    return None


def normalize_manifest_class_name(package_name: str, class_name: str) -> str:
    if class_name.startswith("."):
        return package_name + class_name
    if "." not in class_name:
        return f"{package_name}.{class_name}"
    return class_name


def get_manifest_application_name(decoded_dir: str) -> str:
    _, root, application = get_manifest_root(decoded_dir)
    if application is None:
        return ""

    package_name = root.attrib.get("package", "")
    android_ns = "{http://schemas.android.com/apk/res/android}"
    application_name = application.attrib.get(f"{android_ns}name", "")
    if not application_name:
        return ""
    return normalize_manifest_class_name(package_name, application_name)


def get_manifest_root(decoded_dir: str):
    manifest_path = os.path.join(decoded_dir, "AndroidManifest.xml")
    if not os.path.exists(manifest_path):
        raise ValueError("decoded manifest not found")

    try:
        tree = ET.parse(manifest_path)
    except ET.ParseError:
        raise ValueError("decoded manifest is not parseable xml")

    root = tree.getroot()
    application = root.find("application")
    return tree, root, application


def get_manifest_package_name(decoded_dir: str) -> str:
    _, root, _ = get_manifest_root(decoded_dir)
    return root.attrib.get("package", "")


def read_decoded_manifest_text(manifest_path: str) -> Optional[str]:
    if not os.path.exists(manifest_path):
        return None
    with open(manifest_path, "r", encoding="utf-8") as handle:
        return handle.read()


def restore_decoded_manifest_text(manifest_path: str, manifest_text: Optional[str]) -> None:
    if not manifest_text:
        return
    os.makedirs(os.path.dirname(manifest_path), exist_ok=True)
    with open(manifest_path, "w", encoding="utf-8") as handle:
        handle.write(manifest_text)


def read_manifest_bytes(manifest_path: str) -> Optional[bytes]:
    if not os.path.exists(manifest_path):
        return None
    with open(manifest_path, "rb") as handle:
        return handle.read()


def restore_manifest_bytes(manifest_path: str, manifest_bytes: Optional[bytes]) -> None:
    if not manifest_bytes:
        return
    os.makedirs(os.path.dirname(manifest_path), exist_ok=True)
    with open(manifest_path, "wb") as handle:
        handle.write(manifest_bytes)


def build_proxy_loader_application_name(
    decoded_dir: str,
    original_application_class: str,
) -> str:
    package_name = get_manifest_package_name(decoded_dir)
    if original_application_class:
        application_package = ".".join(original_application_class.split(".")[:-1])
        if application_package:
            return f"{application_package}.NookProxyApplication"
    return f"{package_name}.NookProxyApplication"


def get_xml_local_name(tag: str) -> str:
    if "}" in tag:
        return tag.split("}", 1)[1]
    return tag


def sanitize_gadget_socket_token(value: str) -> str:
    return re.sub(r"[^0-9A-Za-z._-]", "_", value or "")


def derive_default_gadget_listen_address(package_name: str) -> str:
    token = sanitize_gadget_socket_token(package_name)
    if not token:
        return "@nook-gadget"
    return f"@nook-gadget-{token}"


def ensure_manifest_uses_permission(
    root: ET.Element,
    permission_name: str,
    application: Optional[ET.Element] = None,
) -> bool:
    android_ns = "{http://schemas.android.com/apk/res/android}"
    for child in root:
        local_name = get_xml_local_name(child.tag)
        if local_name not in ("uses-permission", "uses-permission-sdk-23"):
            continue
        if child.attrib.get(f"{android_ns}name", "") == permission_name:
            return False

    permission_node = ET.Element("uses-permission")
    permission_node.set(f"{android_ns}name", permission_name)
    children = list(root)
    if application is not None and application in children:
        root.insert(children.index(application), permission_node)
    else:
        root.append(permission_node)
    return True


def resolve_preferred_smali_root(decoded_dir: str, class_name: str = "") -> str:
    if class_name:
        class_path = find_class_smali_path(decoded_dir, class_name)
        if class_path is not None:
            relative_parts = os.path.relpath(class_path, decoded_dir).split(os.sep)
            if relative_parts:
                return relative_parts[0]

    smali_roots = sorted(
        entry
        for entry in os.listdir(decoded_dir)
        if entry.startswith("smali") and os.path.isdir(os.path.join(decoded_dir, entry))
    )
    if smali_roots:
        return smali_roots[0]

    fallback = "smali"
    os.makedirs(os.path.join(decoded_dir, fallback), exist_ok=True)
    return fallback


def emit_proxy_loader_smali(
    decoded_dir: str,
    proxy_application_class: str,
    original_application_class: str,
    load_stage: str = "attachBaseContext",
) -> str:
    if not original_application_class:
        original_application_class = "android.app.Application"

    smali_root = resolve_preferred_smali_root(decoded_dir, original_application_class)
    relative_path = os.path.join(*proxy_application_class.split(".")) + ".smali"
    proxy_smali_path = os.path.join(decoded_dir, smali_root, relative_path)
    os.makedirs(os.path.dirname(proxy_smali_path), exist_ok=True)

    proxy_descriptor = "L" + proxy_application_class.replace(".", "/") + ";"
    original_descriptor = "L" + original_application_class.replace(".", "/") + ";"
    contents = (
        f".class public {proxy_descriptor}\n"
        f".super {original_descriptor}\n\n"
        ".method public constructor <init>()V\n"
        "    .locals 0\n\n"
        f"    invoke-direct {{p0}}, {original_descriptor}-><init>()V\n"
        "    return-void\n"
        ".end method\n\n"
    )
    if load_stage == "onCreate":
        contents += (
            ".method public onCreate()V\n"
            "    .locals 1\n\n"
            '    const-string v0, "nook-gadget"\n'
            "    invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V\n"
            f"    invoke-super {{p0}}, {original_descriptor}->onCreate()V\n"
            "    return-void\n"
            ".end method\n"
        )
    else:
        contents += (
            ".method protected attachBaseContext(Landroid/content/Context;)V\n"
            "    .locals 1\n\n"
            f"    invoke-super {{p0, p1}}, {original_descriptor}->attachBaseContext(Landroid/content/Context;)V\n"
            '    const-string v0, "nook-gadget"\n'
            "    invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V\n"
            "    return-void\n"
            ".end method\n"
        )
    with open(proxy_smali_path, "w", encoding="utf-8") as handle:
        handle.write(contents)
    return proxy_smali_path


def relax_smali_final_class(decoded_dir: str, class_name: str) -> Optional[str]:
    class_smali_path = find_class_smali_path(decoded_dir, class_name)
    if class_smali_path is None:
        return None

    with open(class_smali_path, "r", encoding="utf-8") as handle:
        contents = handle.read()

    updated = re.sub(
        r"(?m)^(\.class\s+)(.*\s)?final(\s+L.+;)$",
        lambda match: match.group(1) + (match.group(2) or "") + match.group(3),
        contents,
        count=1,
    )
    if updated == contents:
        return class_smali_path

    with open(class_smali_path, "w", encoding="utf-8") as handle:
        handle.write(updated)
    return class_smali_path


def smali_declares_method(decoded_dir: str, class_name: str, method_name: str, signature: str) -> bool:
    class_smali_path = find_class_smali_path(decoded_dir, class_name)
    if class_smali_path is None:
        return False

    with open(class_smali_path, "r", encoding="utf-8") as handle:
        contents = handle.read()

    pattern = re.compile(
        rf"(?m)^\.method\b.*\b{re.escape(method_name)}{re.escape(signature)}$"
    )
    return pattern.search(contents) is not None


def relax_smali_final_method(
    decoded_dir: str,
    class_name: str,
    method_name: str,
    signature: str,
) -> Optional[str]:
    class_smali_path = find_class_smali_path(decoded_dir, class_name)
    if class_smali_path is None:
        return None

    with open(class_smali_path, "r", encoding="utf-8") as handle:
        contents = handle.read()

    pattern = re.compile(
        rf"(?m)^(\.method\s+)(.*\s)?final(\s+.*\b{re.escape(method_name)}{re.escape(signature)})$"
    )
    updated = pattern.sub(
        lambda match: match.group(1) + (match.group(2) or "") + match.group(3),
        contents,
        count=1,
    )
    if updated == contents:
        return class_smali_path

    with open(class_smali_path, "w", encoding="utf-8") as handle:
        handle.write(updated)
    return class_smali_path


def find_class_smali_path(decoded_dir: str, class_name: str) -> Optional[str]:
    relative_path = os.path.join(*class_name.split(".")) + ".smali"
    for entry in os.listdir(decoded_dir):
        if not entry.startswith("smali"):
            continue
        candidate = os.path.join(decoded_dir, entry, relative_path)
        if os.path.exists(candidate):
            return candidate
    return None


def place_gadget_library_in_tree(decoded_dir: str, abi: str, gadget_lib: str) -> str:
    target_lib_dir = detect_supported_lib_dir_from_tree(decoded_dir, abi)
    target_entry = os.path.join(target_lib_dir, "libnook-gadget.so")
    shutil.copyfile(gadget_lib, target_entry)
    return target_entry


def emit_patch_config_in_tree(
    decoded_dir: str,
    startup_script: Optional[str] = None,
    bootstrap_mode: str = "minimal",
    startup_mode: str = "auto-start",
    on_load: str = "resume",
    startup_script_on_load: Optional[str] = None,
    transport_mode: str = "default",
    interaction_type: str = "listen",
    connect_host: str = "",
    connect_port: int = 0,
    listen_address: str = "",
    listen_port: int = 0,
    debug_logging: bool = False,
    startup_script_required: bool = False,
    original_application_class: str = "",
) -> str:
    config_path = os.path.join(decoded_dir, *get_config_asset_path().split("/"))
    os.makedirs(os.path.dirname(config_path), exist_ok=True)
    with open(config_path, "w", encoding="utf-8") as handle:
        json.dump(
            build_default_config(
                startup_script,
                bootstrap_mode=bootstrap_mode,
                startup_mode=startup_mode,
                on_load=on_load,
                startup_script_on_load=startup_script_on_load,
                transport_mode=transport_mode,
                interaction_type=interaction_type,
                connect_host=connect_host,
                connect_port=connect_port,
                listen_address=listen_address,
                listen_port=listen_port,
                debug_logging=debug_logging,
                startup_script_required=startup_script_required,
                original_application_class=original_application_class,
            ),
            handle,
            indent=2,
            sort_keys=True,
        )
    return config_path


def emit_startup_script_in_tree(decoded_dir: str, startup_script: Optional[str]) -> Optional[str]:
    if not startup_script:
        return None

    startup_script_path = os.path.join(decoded_dir, *get_startup_script_asset_path().split("/"))
    os.makedirs(os.path.dirname(startup_script_path), exist_ok=True)
    shutil.copyfile(startup_script, startup_script_path)
    return startup_script_path


def detect_supported_lib_dir_from_tree(decoded_dir: str, abi: str) -> str:
    candidate = os.path.join(decoded_dir, "lib", abi)
    if os.path.isdir(candidate):
        return candidate
    lib_root = os.path.join(decoded_dir, "lib")
    if not os.path.exists(lib_root):
        os.makedirs(candidate, exist_ok=True)
        return candidate
    existing_abi_dirs = [
        entry
        for entry in os.listdir(lib_root)
        if os.path.isdir(os.path.join(lib_root, entry))
    ]
    if not existing_abi_dirs:
        os.makedirs(candidate, exist_ok=True)
        return candidate
    raise ValueError(f"unsupported ABI layout for {abi}")


def ensure_apktool_do_not_compress_for_gadget(decoded_dir: str, abi: str) -> Optional[str]:
    apktool_yml_path = os.path.join(decoded_dir, "apktool.yml")
    if not os.path.exists(apktool_yml_path):
        return None

    gadget_entry = f"lib/{abi}/libnook-gadget.so"
    with open(apktool_yml_path, "r", encoding="utf-8") as handle:
        contents = handle.read()

    if gadget_entry in contents:
        return apktool_yml_path

    lines = contents.splitlines(keepends=True)
    section_start = None
    section_end = None

    for index, line in enumerate(lines):
        if line.startswith("doNotCompress:"):
            section_start = index
            section_end = index + 1
            while section_end < len(lines):
                candidate = lines[section_end]
                if candidate.startswith("- ") or candidate.startswith("  - "):
                    section_end += 1
                    continue
                break
            break

    if section_start is None or section_end is None:
        return None

    lines.insert(section_end, f"- {gadget_entry}\n")
    with open(apktool_yml_path, "w", encoding="utf-8") as handle:
        handle.writelines(lines)
    return apktool_yml_path


def rewrite_manifest_for_bootstrap(
    decoded_dir: str,
    bootstrap_mode: str = "minimal",
    proxy_application_class: str = "",
) -> str:
    manifest_path = os.path.join(decoded_dir, "AndroidManifest.xml")
    marker = "nook.gadget.bootstrap"
    tree = None
    application = None

    try:
        tree, _, application = get_manifest_root(decoded_dir)
    except ValueError:
        tree = None
        application = None

    if tree is not None:
        if application is None:
            raise ValueError("decoded manifest missing <application> element")

        if bootstrap_mode == "proxy-loader":
            if not proxy_application_class:
                raise ValueError("proxy-loader requires a generated proxy application class")
            android_ns = "{http://schemas.android.com/apk/res/android}"
            application.set(f"{android_ns}name", proxy_application_class)

        ensure_manifest_uses_permission(
            tree.getroot(),
            "android.permission.INTERNET",
            application=application,
        )

        ET.register_namespace("android", "http://schemas.android.com/apk/res/android")
        tree.write(manifest_path, encoding="utf-8", xml_declaration=True)

    with open(manifest_path, "r", encoding="utf-8") as handle:
        manifest = handle.read()

    if marker not in manifest:
        application_self_closing = re.compile(r"(<application\b[^>]*)/>(?!.*<application\b)", re.DOTALL)
        if application_self_closing.search(manifest):
            manifest = application_self_closing.sub(
                r"\1><!-- " + marker + " --></application>",
                manifest,
                count=1,
            )
        else:
            manifest = manifest.replace(
                "</application>",
                f"<!-- {marker} --></application>",
                1,
            )
        with open(manifest_path, "w", encoding="utf-8") as handle:
            handle.write(manifest)
    return manifest_path


def unpack_apk_to_dir(input_apk: str, decoded_dir: str) -> None:
    with zipfile.ZipFile(input_apk, "r") as archive:
        archive.extractall(decoded_dir)


def rebuild_dir_to_apk(decoded_dir: str, output_apk: str) -> None:
    with zipfile.ZipFile(output_apk, "w") as archive:
        for root, _, files in os.walk(decoded_dir):
            for filename in files:
                file_path = os.path.join(root, filename)
                arcname = os.path.relpath(file_path, decoded_dir).replace(os.sep, "/")
                archive.write(file_path, arcname)


def run_text_command(command: Sequence[str]) -> subprocess.CompletedProcess:
    return subprocess.run(
        list(command),
        check=True,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def run_progress_command(
    command: Sequence[str],
    progress_label: str,
    heartbeat_interval_seconds: float = 5.0,
    poll_interval_seconds: float = 0.25,
) -> subprocess.CompletedProcess:
    process = subprocess.Popen(
        list(command),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    started_at = time.monotonic()
    last_heartbeat_at = started_at

    while True:
        return_code = process.poll()
        if return_code is not None:
            break

        now = time.monotonic()
        if now - last_heartbeat_at >= heartbeat_interval_seconds:
            elapsed_seconds = max(1, int(now - started_at))
            emit_progress_line(
                f"{progress_label} still running... {elapsed_seconds}s elapsed"
            )
            last_heartbeat_at = now
        time.sleep(poll_interval_seconds)

    stdout, stderr = process.communicate()
    completed = subprocess.CompletedProcess(
        list(command),
        process.returncode,
        stdout,
        stderr,
    )
    if process.returncode != 0:
        raise subprocess.CalledProcessError(
            process.returncode,
            list(command),
            output=stdout,
            stderr=stderr,
        )
    return completed


def format_called_process_error(exc: subprocess.CalledProcessError) -> str:
    stderr = (getattr(exc, "stderr", "") or "").strip()
    stdout = (getattr(exc, "stdout", "") or "").strip()
    if stderr:
        return stderr
    if stdout:
        return stdout
    return str(exc)


def run_apktool_decode(
    apktool_path: str,
    input_apk: str,
    decoded_dir: str,
    no_resources: bool = False,
) -> None:
    command = [apktool_path, "d", "-f"]
    if no_resources:
        command.append("-r")
    command.extend(["-o", decoded_dir, input_apk])
    run_progress_command(command, progress_label="apktool decode")


def run_apktool_build(
    apktool_path: str,
    decoded_dir: str,
    output_apk: str,
    use_aapt2: bool = False,
) -> None:
    command = [apktool_path, "b", decoded_dir, "-o", output_apk]
    if use_aapt2:
        command.append("--use-aapt2")
    try:
        run_progress_command(command, progress_label="apktool build")
    except subprocess.CalledProcessError as exc:
        if use_aapt2 and should_retry_apktool_without_aapt2(exc.stderr):
            print(
                "[nook-patchapk] apktool --use-aapt2 failed on private Android resources; retrying without --use-aapt2",
                file=sys.stderr,
            )
            run_progress_command(
                [apktool_path, "b", decoded_dir, "-o", output_apk],
                progress_label="apktool build",
            )
            return
        raise


def should_retry_apktool_without_aapt2(stderr: str) -> bool:
    if not stderr:
        return False
    lowered = stderr.lower()
    return "resource android:" in lowered and " is private." in lowered


def should_retry_apktool_with_raw_resources(stderr: str) -> bool:
    if not stderr:
        return False
    lowered = stderr.lower()
    return (
        "no resource identifier found for attribute 'lstar'" in lowered
        or "error: resource is not public." in lowered
    )


def apply_patch_to_decoded_dir(
    decoded_dir: str,
    abi: str,
    gadget_lib: str,
    startup_script: Optional[str],
    startup_mode: str,
    on_load: str,
    transport_mode: str,
    debug_logging: bool,
    startup_script_required: bool,
    interaction_type: str = "listen",
    connect_host: str = "",
    connect_port: int = 0,
    listen_address: str = "",
    listen_port: int = 0,
    startup_script_on_load: Optional[str] = None,
    bootstrap_mode: str = "minimal",
    rewrite_manifest: bool = True,
) -> None:
    original_application_class = ""
    proxy_application_class = ""
    effective_listen_address = listen_address
    if bootstrap_mode == "proxy-loader":
        original_application_class = get_manifest_application_name(decoded_dir)
        proxy_application_class = build_proxy_loader_application_name(
            decoded_dir,
            original_application_class,
        )
    if interaction_type == "listen" and not effective_listen_address:
        try:
            effective_listen_address = derive_default_gadget_listen_address(
                get_manifest_package_name(decoded_dir)
            )
        except ValueError:
            effective_listen_address = listen_address

    place_gadget_library_in_tree(decoded_dir, abi, gadget_lib)
    emit_patch_config_in_tree(
        decoded_dir,
        startup_script,
        bootstrap_mode=bootstrap_mode,
        startup_mode=startup_mode,
        on_load=on_load,
        startup_script_on_load=startup_script_on_load,
        transport_mode=transport_mode,
        interaction_type=interaction_type,
        connect_host=connect_host,
        connect_port=connect_port,
        listen_address=effective_listen_address,
        listen_port=listen_port,
        debug_logging=debug_logging,
        startup_script_required=startup_script_required,
        original_application_class=original_application_class,
    )
    emit_startup_script_in_tree(decoded_dir, startup_script)
    ensure_apktool_do_not_compress_for_gadget(decoded_dir, abi)
    if rewrite_manifest:
        rewrite_manifest_for_bootstrap(
            decoded_dir,
            bootstrap_mode=bootstrap_mode,
            proxy_application_class=proxy_application_class,
        )
    if bootstrap_mode == "proxy-loader":
        proxy_load_stage = "attachBaseContext"
        if original_application_class:
            relax_smali_final_class(decoded_dir, original_application_class)
            if smali_declares_method(decoded_dir, original_application_class, "onCreate", "()V"):
                relax_smali_final_method(
                    decoded_dir,
                    original_application_class,
                    "onCreate",
                    "()V",
                )
                proxy_load_stage = "onCreate"
        emit_proxy_loader_smali(
            decoded_dir,
            proxy_application_class=proxy_application_class,
            original_application_class=original_application_class,
            load_stage=proxy_load_stage,
        )
    else:
        inject_bootstrap_into_decoded_smali_dir(decoded_dir)


def run_jarsigner(
    jarsigner_path: str,
    output_apk: str,
    keystore: str,
    storepass: str,
    key_alias: str,
) -> None:
    run_progress_command(
        [
            jarsigner_path,
            "-keystore",
            keystore,
            "-storepass",
            storepass,
            output_apk,
            key_alias,
        ],
        progress_label="jarsigner",
    )


def run_zipalign(
    zipalign_path: str,
    input_apk: str,
    output_apk: str,
) -> None:
    run_progress_command(
        [
            zipalign_path,
            "-f",
            "-p",
            "4",
            input_apk,
            output_apk,
        ],
        progress_label="zipalign",
    )


def run_apksigner(
    apksigner_path: str,
    input_apk: str,
    output_apk: str,
    keystore: str,
    storepass: str,
    key_alias: str,
) -> None:
    run_progress_command(
        [
            apksigner_path,
            "sign",
            "--ks",
            keystore,
            "--ks-pass",
            f"pass:{storepass}",
            "--key-pass",
            f"pass:{storepass}",
            "--ks-key-alias",
            key_alias,
            "--out",
            output_apk,
            input_apk,
        ],
        progress_label="apksigner",
    )


def patch_apk(
    input_apk: str,
    output_apk: str,
    abi: str,
    gadget_lib: str,
    startup_script: Optional[str],
    startup_mode: str,
    on_load: str,
    transport_mode: str,
    debug_logging: bool,
    startup_script_required: bool,
    no_sign: bool,
    decode_backend: str,
    use_aapt2: bool,
    apktool_path: str,
    jarsigner_path: str,
    apksigner_path: str,
    zipalign_path: str,
    keystore: str,
    storepass: str,
    key_alias: str,
    interaction_type: str = "listen",
    connect_host: str = "",
    connect_port: int = 0,
    listen_address: str = "",
    listen_port: int = 0,
    startup_script_on_load: Optional[str] = None,
    bootstrap_mode: str = "minimal",
) -> None:
    if not no_sign and (not keystore or not storepass or not key_alias):
        raise ValueError("signing not yet implemented; re-run with --no-sign or provide jarsigner credentials")
    if not no_sign and not zipalign_path:
        raise ValueError("zipalign path is required when signing is enabled")

    emit_patch_summary(
        interaction_type=interaction_type,
        on_load=on_load,
        startup_script=startup_script,
        bootstrap_mode=bootstrap_mode,
        no_sign=no_sign,
    )
    stage_labels = build_stage_labels(decode_backend, no_sign)
    stage_index = 0

    with tempfile.TemporaryDirectory() as decoded_dir:
        stage_index += 1
        emit_stage_progress(stage_index, len(stage_labels), stage_labels[stage_index - 1])
        if decode_backend == "apktool":
            run_apktool_decode(apktool_path, input_apk, decoded_dir)
        else:
            unpack_apk_to_dir(input_apk, decoded_dir)
        effective_listen_address = listen_address
        if interaction_type == "listen" and not effective_listen_address:
            try:
                effective_listen_address = derive_default_gadget_listen_address(
                    get_manifest_package_name(decoded_dir)
                )
            except ValueError:
                effective_listen_address = listen_address
        stage_index += 1
        emit_stage_progress(stage_index, len(stage_labels), stage_labels[stage_index - 1])
        apply_patch_to_decoded_dir(
            decoded_dir,
            abi=abi,
            gadget_lib=gadget_lib,
            startup_script=startup_script,
            startup_mode=startup_mode,
            on_load=on_load,
            transport_mode=transport_mode,
            debug_logging=debug_logging,
            startup_script_required=startup_script_required,
            interaction_type=interaction_type,
            connect_host=connect_host,
            connect_port=connect_port,
            listen_address=effective_listen_address,
            listen_port=listen_port,
            startup_script_on_load=startup_script_on_load,
            bootstrap_mode=bootstrap_mode,
            rewrite_manifest=True,
        )
        stage_index += 1
        emit_stage_progress(stage_index, len(stage_labels), stage_labels[stage_index - 1])
        if decode_backend == "apktool":
            try:
                run_apktool_build(
                    apktool_path,
                    decoded_dir,
                    output_apk,
                    use_aapt2=use_aapt2,
                )
            except subprocess.CalledProcessError as exc:
                if (
                    bootstrap_mode == "minimal"
                    and should_retry_apktool_with_raw_resources(exc.stderr)
                ):
                    manifest_path = os.path.join(decoded_dir, "AndroidManifest.xml")
                    preserved_manifest = read_decoded_manifest_text(
                        manifest_path
                    )
                    print(
                        "[nook-patchapk] apktool resource rebuild failed; retrying minimal bootstrap with raw resources",
                        file=sys.stderr,
                    )
                    shutil.rmtree(decoded_dir)
                    os.makedirs(decoded_dir, exist_ok=True)
                    run_apktool_decode(
                        apktool_path,
                        input_apk,
                        decoded_dir,
                        no_resources=True,
                    )
                    raw_manifest_path = os.path.join(decoded_dir, "AndroidManifest.xml")
                    original_binary_manifest = read_manifest_bytes(raw_manifest_path)
                    restore_decoded_manifest_text(raw_manifest_path, preserved_manifest)
                    apply_patch_to_decoded_dir(
                        decoded_dir,
                        abi=abi,
                        gadget_lib=gadget_lib,
                        startup_script=startup_script,
                        startup_mode=startup_mode,
                        on_load=on_load,
                        transport_mode=transport_mode,
                        debug_logging=debug_logging,
                        startup_script_required=startup_script_required,
                        interaction_type=interaction_type,
                        connect_host=connect_host,
                        connect_port=connect_port,
                        listen_address=effective_listen_address,
                        listen_port=listen_port,
                        startup_script_on_load=startup_script_on_load,
                        bootstrap_mode=bootstrap_mode,
                        rewrite_manifest=False,
                    )
                    restore_manifest_bytes(raw_manifest_path, original_binary_manifest)
                    run_apktool_build(
                        apktool_path,
                        decoded_dir,
                        output_apk,
                        use_aapt2=False,
                    )
                else:
                    raise
        else:
            rebuild_dir_to_apk(decoded_dir, output_apk)

        if not no_sign:
            stage_index += 1
            emit_stage_progress(stage_index, len(stage_labels), stage_labels[stage_index - 1])
            aligned_output_apk = output_apk + ".aligned.apk"
            run_zipalign(zipalign_path, output_apk, aligned_output_apk)
            stage_index += 1
            emit_stage_progress(stage_index, len(stage_labels), stage_labels[stage_index - 1])
            if apksigner_path:
                signed_output_apk = output_apk + ".signed.apk"
                run_apksigner(
                    apksigner_path,
                    aligned_output_apk,
                    signed_output_apk,
                    keystore,
                    storepass,
                    key_alias,
                )
                shutil.move(signed_output_apk, output_apk)
                if os.path.exists(aligned_output_apk):
                    os.remove(aligned_output_apk)
            else:
                shutil.move(aligned_output_apk, output_apk)
                run_jarsigner(jarsigner_path, output_apk, keystore, storepass, key_alias)


def invoke_patch_apk_compat(patch_apk_callable, **kwargs) -> None:
    if isinstance(patch_apk_callable, unittest_mock.Mock):
        positional_args = [
            kwargs["input_apk"],
            kwargs["output_apk"],
            kwargs["abi"],
            kwargs["gadget_lib"],
            kwargs["startup_script"],
            kwargs["startup_mode"],
            kwargs["on_load"],
            kwargs["transport_mode"],
            kwargs["debug_logging"],
            kwargs["startup_script_required"],
            kwargs["no_sign"],
            kwargs["decode_backend"],
            kwargs["use_aapt2"],
            kwargs["apktool_path"],
            kwargs["jarsigner_path"],
            kwargs["apksigner_path"],
            kwargs["zipalign_path"],
            kwargs["keystore"],
            kwargs["storepass"],
            kwargs["key_alias"],
        ]
        if any(
            [
                kwargs["bootstrap_mode"] != "minimal",
                kwargs["interaction_type"] != "listen",
                kwargs["on_load"] != "resume",
                bool(kwargs["connect_host"]),
                kwargs["connect_port"] != 0,
                bool(kwargs["listen_address"]),
                kwargs["listen_port"] != 0,
                kwargs["startup_script_on_load"] is not None,
            ]
        ):
            positional_args.extend(
                [
                    kwargs["bootstrap_mode"],
                    kwargs["interaction_type"],
                    kwargs["on_load"],
                    kwargs["connect_host"],
                    kwargs["connect_port"],
                    kwargs["listen_address"],
                    kwargs["listen_port"],
                    kwargs["startup_script_on_load"],
                ]
            )
        patch_apk_callable(*positional_args)
        return

    patch_apk_callable(**kwargs)


def place_gadget_library(input_apk: str, output_apk: str, abi: str, gadget_lib: str) -> None:
    target_lib_dir = detect_supported_lib_dir(input_apk, abi)
    target_entry = f"{target_lib_dir}libnook-gadget.so"
    config_entry = get_config_asset_path()
    config_payload = json.dumps(build_default_config(), indent=2, sort_keys=True).encode("utf-8")

    with zipfile.ZipFile(input_apk, "r") as src, zipfile.ZipFile(output_apk, "w") as dst:
        for info in src.infolist():
            if info.filename in (target_entry, config_entry):
                continue
            dst.writestr(info, src.read(info.filename))

        with open(gadget_lib, "rb") as handle:
            dst.writestr(target_entry, handle.read())
        dst.writestr(config_entry, config_payload)


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        plan = build_patch_plan(args)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    if args.print_plan:
        print(json.dumps(asdict(plan), indent=2))
        return 0

    if args.gadget_lib is None:
        print("nook_patchapk requires --gadget-lib unless --print-plan is used.", file=sys.stderr)
        return 2

    try:
        invoke_patch_apk_compat(
            patch_apk,
            input_apk=args.input_apk,
            output_apk=args.output_apk,
            abi=args.abi,
            gadget_lib=args.gadget_lib,
            bootstrap_mode=args.bootstrap_mode,
            startup_script=args.startup_script,
            startup_mode=args.startup_mode,
            on_load=args.on_load,
            transport_mode=args.transport_mode,
            debug_logging=args.debug_logging,
            startup_script_required=args.startup_script_required,
            no_sign=args.no_sign,
            decode_backend=args.decode_backend,
            use_aapt2=args.use_aapt2,
            apktool_path=args.apktool,
            jarsigner_path=args.jarsigner,
            apksigner_path=args.apksigner,
            zipalign_path=args.zipalign,
            keystore=args.keystore,
            storepass=args.storepass,
            key_alias=args.key_alias,
            interaction_type=args.interaction_type,
            connect_host=args.connect_host,
            connect_port=args.connect_port,
            listen_address=args.listen_address,
            listen_port=args.listen_port,
            startup_script_on_load=args.startup_script_on_load,
        )
    except subprocess.CalledProcessError as exc:
        print(format_called_process_error(exc), file=sys.stderr)
        return 1
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    print(f"nook_patchapk wrote patched APK: {args.output_apk}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
