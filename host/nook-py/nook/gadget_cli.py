import argparse
import importlib.util
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import List, Optional, Sequence


DEFAULT_APKTOOL = r"E:\Re_tools\APKTool\apktool.bat"
DEFAULT_APKSIGNER = r"E:\SDK\build-tools\34.0.0\apksigner.bat"
DEFAULT_ZIPALIGN = r"E:\SDK\build-tools\34.0.0\zipalign.exe"


def default_output_apk(source: Path, repo_root: Optional[Path] = None) -> Path:
    if repo_root is not None:
        output_dir = repo_root / "build"
        output_dir.mkdir(parents=True, exist_ok=True)
        return output_dir / f"{source.stem}-nook.apk"
    return source.with_name(f"{source.stem}-nook.apk")


def _resolve_env_or_default(env_name: str, default_value: str) -> str:
    return os.environ.get(env_name, default_value)


def _iter_repo_root_candidates() -> List[Path]:
    candidates: List[Path] = []
    seen = set()

    for seed in (Path.cwd(), Path(__file__).resolve()):
        for candidate in (seed, *seed.parents):
            key = str(candidate)
            if key in seen:
                continue
            seen.add(key)
            candidates.append(candidate)

    return candidates


def find_repo_root() -> Path:
    for candidate in _iter_repo_root_candidates():
        patch_tool = candidate / "tools" / "nook_patchapk.py"
        build_script = candidate / "tools" / "build_nook_gadget.ps1"
        if patch_tool.exists() and build_script.exists():
            return candidate

    raise RuntimeError(
        "cannot locate the Nook repo root; run nook-gadget inside a full Nook checkout"
    )


def _run_powershell_script(script_path: Path, extra_args: Sequence[str]) -> None:
    command = [
        "powershell",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        str(script_path),
        *extra_args,
    ]
    subprocess.run(command, check=True)


def _build_adb_command(serial: str, *extra_args: str) -> List[str]:
    command = ["adb"]
    if serial:
        command.extend(["-s", serial])
    command.extend(extra_args)
    return command


def _run_adb(serial: str, *extra_args: str) -> None:
    subprocess.run(_build_adb_command(serial, *extra_args), check=True)


def _load_patch_tool(repo_root: Path):
    patch_tool = repo_root / "tools" / "nook_patchapk.py"
    spec = importlib.util.spec_from_file_location("nook_patchapk_tool", patch_tool)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"failed to load patch tool: {patch_tool}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _resolve_gadget_lib(repo_root: Path, cli_value: str) -> Path:
    if cli_value:
        return Path(cli_value)

    env_value = os.environ.get("NOOK_GADGET_LIB", "")
    if env_value:
        return Path(env_value)

    return repo_root / "libs" / "arm64-v8a" / "libnook-gadget.so"


def _resolve_keystore(repo_root: Path, cli_value: str) -> Path:
    if cli_value:
        return Path(cli_value)

    env_value = os.environ.get("NOOK_GADGET_KEYSTORE", "")
    if env_value:
        return Path(env_value)

    return repo_root / "build" / "keystore" / "nook-debug.keystore"


def _ensure_gadget_lib(repo_root: Path, gadget_lib: Path, no_build: bool) -> None:
    if gadget_lib.exists():
        return
    if no_build:
        raise RuntimeError(f"gadget library not found: {gadget_lib}")

    _run_powershell_script(
        repo_root / "tools" / "build_nook_gadget.ps1",
        [],
    )
    if not gadget_lib.exists():
        raise RuntimeError(f"gadget library not found after build: {gadget_lib}")


def _ensure_keystore(
    repo_root: Path,
    keystore: Path,
    storepass: str,
    key_alias: str,
) -> None:
    if keystore.exists():
        return

    _run_powershell_script(
        repo_root / "tools" / "ensure_nook_debug_keystore.ps1",
        [
            "-KeystorePath",
            str(keystore),
            "-Storepass",
            storepass,
            "-Keypass",
            storepass,
            "-KeyAlias",
            key_alias,
        ],
    )
    if not keystore.exists():
        raise RuntimeError(f"keystore not found after provisioning: {keystore}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="nook-gadget",
        description="Nook gadget helper commands.",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    patch_parser = subparsers.add_parser(
        "patchapk",
        help="Patch an APK with libnook-gadget.so using a short objection-like command.",
    )
    patch_parser.add_argument("--source", required=True, help="Path to the source APK.")
    patch_parser.add_argument("--output", default="", help="Path to the patched APK.")
    patch_parser.add_argument(
        "--architecture",
        default="arm64-v8a",
        help="Target gadget ABI. Current primary support is arm64-v8a.",
    )
    patch_parser.add_argument(
        "--startup-script",
        default="",
        help="Optional startup script packaged into the APK.",
    )
    patch_parser.add_argument(
        "--startup-script-on-load",
        choices=["auto", "manual"],
        help="Packaged startup script auto-load policy.",
    )
    patch_parser.add_argument(
        "--startup-script-required",
        action="store_true",
        help="Require the packaged startup script to load successfully.",
    )
    patch_parser.add_argument(
        "--bootstrap-mode",
        default="minimal",
        choices=["minimal", "proxy-loader"],
        help="APK bootstrap mode.",
    )
    patch_parser.add_argument(
        "--startup-mode",
        default="auto-start",
        choices=["auto-start", "manual"],
        help="Gadget startup mode.",
    )
    patch_parser.add_argument(
        "--on-load",
        default="resume",
        choices=["resume", "wait"],
        help="Gadget listen on-load policy.",
    )
    patch_parser.add_argument(
        "--transport-mode",
        default="default",
        choices=["default"],
        help="Transport metadata written into gadget config.",
    )
    patch_parser.add_argument(
        "--interaction-type",
        default="listen",
        choices=["listen", "connect"],
        help="Gadget interaction type.",
    )
    patch_parser.add_argument("--connect-host", default="127.0.0.1", help="Connect host.")
    patch_parser.add_argument("--connect-port", type=int, default=27042, help="Connect port.")
    patch_parser.add_argument("--listen-address", default="", help="Listen address.")
    patch_parser.add_argument("--listen-port", type=int, default=0, help="Listen port.")
    patch_parser.add_argument(
        "--decode-backend",
        default="apktool",
        choices=["apktool", "internal-zip"],
        help="APK decode/rebuild backend.",
    )
    patch_parser.add_argument(
        "--use-aapt2",
        action="store_true",
        help="Pass --use-aapt2 through to apktool build.",
    )
    patch_parser.add_argument(
        "--apktool",
        default=_resolve_env_or_default("NOOK_APKTOOL", DEFAULT_APKTOOL),
        help="Path to apktool.",
    )
    patch_parser.add_argument(
        "--jarsigner",
        default=_resolve_env_or_default("NOOK_JARSIGNER", "jarsigner"),
        help="Path to jarsigner.",
    )
    patch_parser.add_argument(
        "--apksigner",
        default=_resolve_env_or_default("NOOK_APKSIGNER", DEFAULT_APKSIGNER),
        help="Path to apksigner.",
    )
    patch_parser.add_argument(
        "--zipalign",
        default=_resolve_env_or_default("NOOK_ZIPALIGN", DEFAULT_ZIPALIGN),
        help="Path to zipalign.",
    )
    patch_parser.add_argument("--gadget-lib", default="", help="Path to libnook-gadget.so.")
    patch_parser.add_argument("--keystore", default="", help="Path to signing keystore.")
    patch_parser.add_argument(
        "--storepass",
        default=os.environ.get("NOOK_GADGET_STOREPASS", "android"),
        help="Keystore password.",
    )
    patch_parser.add_argument(
        "--key-alias",
        default=os.environ.get("NOOK_GADGET_KEYALIAS", "androiddebugkey"),
        help="Signing key alias.",
    )
    patch_parser.add_argument(
        "--no-sign",
        action="store_true",
        help="Skip APK signing.",
    )
    patch_parser.add_argument(
        "--no-build",
        action="store_true",
        help="Do not auto-build libnook-gadget.so when missing.",
    )
    patch_parser.add_argument(
        "--debug-logging",
        action="store_true",
        help="Enable gadget debug logging in emitted config.",
    )
    patch_parser.add_argument(
        "--print-plan",
        action="store_true",
        help="Print the underlying patch plan JSON and exit.",
    )
    patch_parser.add_argument(
        "--print-cmd",
        action="store_true",
        help="Print the resolved nook_patchapk.py arguments and exit.",
    )

    install_parser = subparsers.add_parser(
        "install",
        help="Install a patched APK onto a device.",
    )
    install_parser.add_argument("--apk", required=True, help="Path to the APK to install.")
    install_parser.add_argument("--serial", default="", help="ADB device serial.")
    install_parser.add_argument(
        "--no-reinstall",
        action="store_true",
        help="Do not pass -r to adb install.",
    )
    install_parser.add_argument(
        "--no-test",
        action="store_true",
        help="Do not pass -t to adb install.",
    )

    launch_parser = subparsers.add_parser(
        "launch",
        help="Launch an installed app for gadget validation.",
    )
    launch_parser.add_argument("--package", required=True, help="Android package name.")
    launch_parser.add_argument(
        "--activity",
        required=True,
        help="Activity name, for example .MainActivity or com.demo.target.MainActivity.",
    )
    launch_parser.add_argument("--serial", default="", help="ADB device serial.")
    launch_parser.add_argument(
        "--stop-first",
        action="store_true",
        help="Force-stop the target package before launch.",
    )
    launch_parser.add_argument(
        "--clear-logcat",
        action="store_true",
        help="Clear logcat before launch.",
    )
    launch_parser.add_argument(
        "--wait",
        type=int,
        default=0,
        help="Sleep N seconds after launch.",
    )
    return parser


def _build_patchapk_argv(args, repo_root: Path) -> List[str]:
    source = Path(args.source)
    output = Path(args.output) if args.output else default_output_apk(source, repo_root=repo_root)
    gadget_lib = _resolve_gadget_lib(repo_root, args.gadget_lib)

    argv = [
        "--input-apk",
        str(source),
        "--output-apk",
        str(output),
        "--abi",
        args.architecture,
        "--bootstrap-mode",
        args.bootstrap_mode,
        "--startup-mode",
        args.startup_mode,
        "--on-load",
        args.on_load,
        "--transport-mode",
        args.transport_mode,
        "--interaction-type",
        args.interaction_type,
        "--decode-backend",
        args.decode_backend,
        "--apktool",
        args.apktool,
        "--jarsigner",
        args.jarsigner,
        "--apksigner",
        args.apksigner,
        "--zipalign",
        args.zipalign,
    ]

    if args.startup_script:
        argv.extend(["--startup-script", args.startup_script])
    if args.startup_script_on_load:
        argv.extend(["--startup-script-on-load", args.startup_script_on_load])
    if args.startup_script_required:
        argv.append("--startup-script-required")
    if args.debug_logging:
        argv.append("--debug-logging")
    if args.use_aapt2:
        argv.append("--use-aapt2")
    if args.interaction_type == "connect":
        argv.extend(["--connect-host", args.connect_host, "--connect-port", str(args.connect_port)])
    else:
        if args.listen_address:
            argv.extend(["--listen-address", args.listen_address])
        if args.listen_port > 0:
            argv.extend(["--listen-port", str(args.listen_port)])

    if args.print_plan:
        argv.append("--print-plan")
        if gadget_lib.exists():
            argv.extend(["--gadget-lib", str(gadget_lib)])
        return argv

    argv.extend(["--gadget-lib", str(gadget_lib)])
    if args.no_sign:
        argv.append("--no-sign")
    else:
        keystore = _resolve_keystore(repo_root, args.keystore)
        argv.extend(
            [
                "--keystore",
                str(keystore),
                "--storepass",
                args.storepass,
                "--key-alias",
                args.key_alias,
            ]
        )
    return argv


def _validate_patchapk_args(args) -> None:
    if args.use_aapt2 and args.decode_backend != "apktool":
        raise RuntimeError("--use-aapt2 requires --decode-backend apktool")


def _handle_patchapk(args) -> int:
    _validate_patchapk_args(args)
    repo_root = find_repo_root()
    gadget_lib = _resolve_gadget_lib(repo_root, args.gadget_lib)

    if not args.print_plan:
        _ensure_gadget_lib(repo_root, gadget_lib, args.no_build)
        if not args.no_sign:
            keystore = _resolve_keystore(repo_root, args.keystore)
            _ensure_keystore(repo_root, keystore, args.storepass, args.key_alias)

    argv = _build_patchapk_argv(args, repo_root)
    if args.print_cmd:
        print("nook_patchapk.py " + " ".join(argv))
        return 0

    patch_tool = _load_patch_tool(repo_root)
    return int(patch_tool.main(argv))


def _handle_install(args) -> int:
    command = ["install"]
    if not args.no_reinstall:
        command.append("-r")
    if not args.no_test:
        command.append("-t")
    command.append(args.apk)
    _run_adb(args.serial, *command)
    return 0


def _resolve_launch_component(package: str, activity: str) -> str:
    if "/" in activity:
        return activity
    if activity.startswith("."):
        return f"{package}/{activity}"
    return f"{package}/{activity}"


def _handle_launch(args) -> int:
    if args.stop_first:
        _run_adb(args.serial, "shell", "am", "force-stop", args.package)
    if args.clear_logcat:
        _run_adb(args.serial, "logcat", "-c")
    _run_adb(
        args.serial,
        "shell",
        "am",
        "start",
        "-n",
        _resolve_launch_component(args.package, args.activity),
    )
    if args.wait > 0:
        time.sleep(args.wait)
    return 0


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.command == "patchapk":
        try:
            return _handle_patchapk(args)
        except subprocess.CalledProcessError as exc:
            if exc.stdout:
                print(exc.stdout, file=sys.stderr, end="" if exc.stdout.endswith("\n") else "\n")
            if exc.stderr:
                print(exc.stderr, file=sys.stderr, end="" if exc.stderr.endswith("\n") else "\n")
            print(str(exc), file=sys.stderr)
            return int(exc.returncode or 1)
        except RuntimeError as exc:
            print(str(exc), file=sys.stderr)
            return 1

    if args.command == "install":
        try:
            return _handle_install(args)
        except subprocess.CalledProcessError as exc:
            if exc.stdout:
                print(exc.stdout, file=sys.stderr, end="" if exc.stdout.endswith("\n") else "\n")
            if exc.stderr:
                print(exc.stderr, file=sys.stderr, end="" if exc.stderr.endswith("\n") else "\n")
            print(str(exc), file=sys.stderr)
            return int(exc.returncode or 1)

    if args.command == "launch":
        try:
            return _handle_launch(args)
        except subprocess.CalledProcessError as exc:
            if exc.stdout:
                print(exc.stdout, file=sys.stderr, end="" if exc.stdout.endswith("\n") else "\n")
            if exc.stderr:
                print(exc.stderr, file=sys.stderr, end="" if exc.stderr.endswith("\n") else "\n")
            print(str(exc), file=sys.stderr)
            return int(exc.returncode or 1)

    parser.error(f"unsupported command: {args.command}")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
