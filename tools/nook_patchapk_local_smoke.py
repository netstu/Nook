#!/usr/bin/env python3

import argparse
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from typing import List, Optional


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="nook_patchapk_local_smoke",
        description="Run a local no-device smoke test for tools/nook_patchapk.py.",
    )
    parser.add_argument(
        "--python",
        default=sys.executable,
        help="Python interpreter used to invoke tools/nook_patchapk.py.",
    )
    parser.add_argument(
        "--patch-tool",
        default="tools/nook_patchapk.py",
        help="Path to the nook_patchapk.py entrypoint.",
    )
    parser.add_argument(
        "--gadget-lib",
        default="libs/arm64-v8a/libnook-gadget.so",
        help="Path to libnook-gadget.so used by the smoke test.",
    )
    parser.add_argument(
        "--bootstrap-mode",
        default="minimal",
        choices=["minimal", "proxy-loader"],
        help="Bootstrap mode exercised by the local smoke test.",
    )
    parser.add_argument(
        "--keep-temp",
        action="store_true",
        help="Keep the temporary workspace for inspection.",
    )
    return parser


def write_minimal_fixture_tree(root: Path) -> Path:
    src = root / "src"
    smali_dir = src / "smali" / "com" / "example" / "app"
    smali_dir.mkdir(parents=True, exist_ok=True)

    (src / "AndroidManifest.xml").write_text(
        """<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.example.app">
  <application>
    <activity android:name=".MainActivity">
      <intent-filter>
        <action android:name="android.intent.action.MAIN" />
        <category android:name="android.intent.category.LAUNCHER" />
      </intent-filter>
    </activity>
  </application>
</manifest>
""",
        encoding="utf-8",
    )

    (smali_dir / "MainActivity.smali").write_text(
        """.class public Lcom/example/app/MainActivity;
.super Landroid/app/Activity;

.method protected onCreate(Landroid/os/Bundle;)V
    .locals 1

    invoke-super {p0, p1}, Landroid/app/Activity;->onCreate(Landroid/os/Bundle;)V
    return-void
.end method
""",
        encoding="utf-8",
    )

    startup_script = root / "startup.js"
    startup_script.write_text('send("hello-from-gadget-smoke");\n', encoding="utf-8")
    return startup_script


def write_proxy_loader_fixture_tree(root: Path) -> Path:
    src = root / "src"
    smali_dir = src / "smali" / "com" / "example" / "app"
    smali_dir.mkdir(parents=True, exist_ok=True)

    (src / "AndroidManifest.xml").write_text(
        """<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.example.app">
  <application android:name=".MyApplication">
    <activity android:name=".MainActivity">
      <intent-filter>
        <action android:name="android.intent.action.MAIN" />
        <category android:name="android.intent.category.LAUNCHER" />
      </intent-filter>
    </activity>
  </application>
</manifest>
""",
        encoding="utf-8",
    )

    (smali_dir / "MainActivity.smali").write_text(
        """.class public Lcom/example/app/MainActivity;
.super Landroid/app/Activity;

.method protected onCreate(Landroid/os/Bundle;)V
    .locals 1

    invoke-super {p0, p1}, Landroid/app/Activity;->onCreate(Landroid/os/Bundle;)V
    return-void
.end method
""",
        encoding="utf-8",
    )

    (smali_dir / "MyApplication.smali").write_text(
        """.class public final Lcom/example/app/MyApplication;
.super Landroid/app/Application;

.method public constructor <init>()V
    .locals 0

    invoke-direct {p0}, Landroid/app/Application;-><init>()V
    return-void
.end method

.method public final onCreate()V
    .locals 0

    invoke-super {p0}, Landroid/app/Application;->onCreate()V
    return-void
.end method
""",
        encoding="utf-8",
    )

    startup_script = root / "startup.js"
    startup_script.write_text('send("hello-from-gadget-smoke");\n', encoding="utf-8")
    return startup_script


def build_fixture_apk(src_dir: Path, output_apk: Path) -> None:
    with zipfile.ZipFile(output_apk, "w") as archive:
        for path in src_dir.rglob("*"):
            if path.is_file():
                archive.write(path, path.relative_to(src_dir).as_posix())


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def validate_minimal_patched_apk(output_apk: Path) -> None:
    with zipfile.ZipFile(output_apk, "r") as archive:
        names = set(archive.namelist())
        required_entries = {
            "assets/nook-gadget/config.json",
            "assets/nook-gadget/startup.js",
            "lib/arm64-v8a/libnook-gadget.so",
            "smali/com/example/app/MainActivity.smali",
        }
        missing = sorted(required_entries - names)
        require(not missing, f"patched apk missing expected entries: {missing}")

        smali_text = archive.read("smali/com/example/app/MainActivity.smali").decode("utf-8")
        config_text = archive.read("assets/nook-gadget/config.json").decode("utf-8")
        startup_text = archive.read("assets/nook-gadget/startup.js").decode("utf-8")

        require("nook-gadget" in smali_text, "patched smali missing nook-gadget literal")
        require("System;->loadLibrary" in smali_text, "patched smali missing System.loadLibrary injection")
        require('"startup_script"' in config_text, "patched config missing startup_script section")
        require('"interaction"' in config_text, "patched config missing interaction section")
        require("hello-from-gadget-smoke" in startup_text, "patched startup asset missing expected payload")


def validate_proxy_loader_patched_apk(output_apk: Path) -> None:
    with zipfile.ZipFile(output_apk, "r") as archive:
        names = set(archive.namelist())
        required_entries = {
            "assets/nook-gadget/config.json",
            "assets/nook-gadget/startup.js",
            "lib/arm64-v8a/libnook-gadget.so",
            "smali/com/example/app/MyApplication.smali",
            "smali/com/example/app/NookProxyApplication.smali",
        }
        missing = sorted(required_entries - names)
        require(not missing, f"proxy-loader patched apk missing expected entries: {missing}")

        manifest_text = archive.read("AndroidManifest.xml").decode("utf-8")
        config_text = archive.read("assets/nook-gadget/config.json").decode("utf-8")
        proxy_smali_text = archive.read("smali/com/example/app/NookProxyApplication.smali").decode("utf-8")
        original_smali_text = archive.read("smali/com/example/app/MyApplication.smali").decode("utf-8")

        require("com.example.app.NookProxyApplication" in manifest_text,
                "proxy-loader manifest rewrite missing proxy application class")
        require("nook.gadget.bootstrap" in manifest_text,
                "proxy-loader manifest rewrite missing bootstrap marker")
        require('"proxy-loader"' in config_text,
                "proxy-loader config missing bootstrap_mode")
        require('"original_application_class"' in config_text,
                "proxy-loader config missing original_application_class metadata")
        require("System;->loadLibrary" in proxy_smali_text,
                "proxy-loader smali missing System.loadLibrary injection")
        require("Lcom/example/app/MyApplication;" in proxy_smali_text,
                "proxy-loader smali must inherit from the original application class")
        require(".class public" in original_smali_text and "final" not in original_smali_text.splitlines()[0],
                "proxy-loader patch must relax final on the original application class")
        require(".method public" in original_smali_text and "final" not in original_smali_text,
                "proxy-loader patch must relax final on the original onCreate method")


def main(argv: Optional[List[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    patch_tool = Path(args.patch_tool)
    gadget_lib = Path(args.gadget_lib)

    require(patch_tool.exists(), f"patch tool not found: {patch_tool}")
    require(gadget_lib.exists(), f"gadget library not found: {gadget_lib}")

    temp_root = Path(tempfile.mkdtemp(prefix="nook_patchapk_local_smoke_"))
    try:
        if args.bootstrap_mode == "proxy-loader":
            startup_script = write_proxy_loader_fixture_tree(temp_root)
        else:
            startup_script = write_minimal_fixture_tree(temp_root)
        input_apk = temp_root / "fixture.apk"
        output_apk = temp_root / "patched.apk"
        build_fixture_apk(temp_root / "src", input_apk)

        subprocess.run(
            [
                args.python,
                str(patch_tool),
                "--input-apk",
                str(input_apk),
                "--output-apk",
                str(output_apk),
                "--gadget-lib",
                str(gadget_lib),
                "--bootstrap-mode",
                args.bootstrap_mode,
                "--startup-script",
                str(startup_script),
                "--no-sign",
            ],
            check=True,
        )

        if args.bootstrap_mode == "proxy-loader":
            validate_proxy_loader_patched_apk(output_apk)
        else:
            validate_minimal_patched_apk(output_apk)
        print(
            f"[nook-patchapk-local-smoke] ok bootstrap_mode={args.bootstrap_mode} temp_root={temp_root}"
        )
        return 0
    except Exception as exc:
        print(f"[nook-patchapk-local-smoke] failed: {exc}", file=sys.stderr)
        print(f"[nook-patchapk-local-smoke] temp_root={temp_root}", file=sys.stderr)
        return 1
    finally:
        if args.keep_temp:
            print(f"[nook-patchapk-local-smoke] kept temp_root={temp_root}")
        else:
            shutil.rmtree(temp_root, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
