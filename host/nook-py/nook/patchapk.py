import importlib.util
import os
import shutil
import subprocess
import sys
from types import SimpleNamespace


def _repo_root() -> str:
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def _which(program: str):
    return shutil.which(program)


def _env_or_empty(name: str) -> str:
    return os.environ.get(name, "").strip()


def _iter_existing_files(candidates):
    seen = set()
    for candidate in candidates:
        normalized = (candidate or "").strip()
        if not normalized or normalized in seen:
            continue
        seen.add(normalized)
        if os.path.isfile(normalized):
            return normalized
    return ""


def _default_gadget_lib() -> str:
    return os.path.join(_repo_root(), "libs", "arm64-v8a", "libnook-gadget.so")


def _default_keystore() -> str:
    return os.path.join(_repo_root(), "build", "keystore", "nook-debug.keystore")


def _load_patch_tool():
    patch_tool_path = os.path.join(_repo_root(), "tools", "nook_patchapk.py")
    spec = importlib.util.spec_from_file_location("nook_patchapk_tool", patch_tool_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"failed to load patch tool: {patch_tool_path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _iter_android_build_tools_filenames(filename: str):
    env_roots = [
        _env_or_empty("ANDROID_HOME"),
        _env_or_empty("ANDROID_SDK_ROOT"),
    ]
    legacy_roots = [
        r"E:\SDK",
        r"C:\Android\Sdk",
        os.path.join(os.path.expanduser("~"), "AppData", "Local", "Android", "Sdk"),
    ]
    seen_roots = set()
    for root in [*env_roots, *legacy_roots]:
        if not root or root in seen_roots:
            continue
        seen_roots.add(root)
        build_tools_dir = os.path.join(root, "build-tools")
        if not os.path.isdir(build_tools_dir):
            continue
        try:
            versions = sorted(os.listdir(build_tools_dir), reverse=True)
        except OSError:
            continue
        for version in versions:
            yield os.path.join(build_tools_dir, version, filename)


def _resolve_tool(explicit_value: str, env_name: str, fallback_name: str, error_message: str, extra_candidates=None) -> str:
    candidate = (explicit_value or "").strip()
    if candidate:
        return candidate
    env_candidate = _env_or_empty(env_name)
    if env_candidate:
        return env_candidate
    path_candidate = _which(fallback_name)
    if path_candidate:
        return path_candidate
    file_candidate = _iter_existing_files(extra_candidates or [])
    if file_candidate:
        return file_candidate
    raise ValueError(error_message)


def _invoke_nook_patchapk(argv):
    patch_tool = _load_patch_tool()
    exit_code = patch_tool.main(argv)
    if exit_code:
        raise SystemExit(exit_code)
    return exit_code


def _adb_base_args(usb: bool = False, serial: str = None):
    args = ["adb"]
    if usb:
        args.append("-d")
    if serial:
        args.extend(["-s", serial])
    return args


def _adb_install(output_apk: str, usb: bool = False, serial: str = None) -> None:
    subprocess.run(
        _adb_base_args(usb=usb, serial=serial) + ["install", "-r", "-t", output_apk],
        check=True,
    )


def _adb_launch(package_name: str, usb: bool = False, serial: str = None) -> None:
    subprocess.run(
        _adb_base_args(usb=usb, serial=serial)
        + ["shell", "monkey", "-p", package_name, "-c", "android.intent.category.LAUNCHER", "1"],
        check=True,
    )


def run_patchapk(options):
    repo_root = _repo_root()
    if options.launch and not options.install:
        raise ValueError("--launch requires --install")

    apktool = _resolve_tool(
        getattr(options, "apktool", ""),
        "NOOK_APKTOOL",
        "apktool",
        "apktool not found; install apktool or pass --apktool <path>",
        extra_candidates=[
            r"E:\Re_tools\APKTool\apktool.bat",
        ],
    )

    argv = [
        "--input-apk",
        options.input_apk,
        "--output-apk",
        options.output_apk,
        "--gadget-lib",
        _default_gadget_lib(),
        "--bootstrap-mode",
        options.bootstrap,
        "--startup-mode",
        options.startup_mode,
        "--on-load",
        options.on_load,
        "--interaction-type",
        options.interaction,
        "--decode-backend",
        options.decode_backend,
        "--apktool",
        apktool,
    ]

    if options.startup_script:
        argv.extend(["--startup-script", options.startup_script])

    if options.interaction == "connect":
        argv.extend(
            [
                "--connect-host",
                options.connect_host,
                "--connect-port",
                str(options.connect_port),
            ]
        )
    else:
        if options.listen_address:
            argv.extend(["--listen-address", options.listen_address])
        if options.listen_port > 0:
            argv.extend(["--listen-port", str(options.listen_port)])

    if options.sign:
        apksigner = _resolve_tool(
            getattr(options, "apksigner", ""),
            "NOOK_APKSIGNER",
            "apksigner",
            "apksigner not found; install Android build-tools or pass --apksigner <path>",
            extra_candidates=_iter_android_build_tools_filenames("apksigner.bat"),
        )
        zipalign = _resolve_tool(
            getattr(options, "zipalign", ""),
            "NOOK_ZIPALIGN",
            "zipalign",
            "zipalign not found; install Android build-tools or pass --zipalign <path>",
            extra_candidates=_iter_android_build_tools_filenames("zipalign.exe"),
        )
        argv.extend(
            [
                "--apksigner",
                apksigner,
                "--zipalign",
                zipalign,
                "--keystore",
                _default_keystore(),
                "--storepass",
                "android",
                "--key-alias",
                "androiddebugkey",
            ]
        )
    else:
        explicit_apksigner = (getattr(options, "apksigner", "") or "").strip()
        explicit_zipalign = (getattr(options, "zipalign", "") or "").strip()
        if explicit_apksigner:
            argv.extend(["--apksigner", explicit_apksigner])
        if explicit_zipalign:
            argv.extend(["--zipalign", explicit_zipalign])
        argv.append("--no-sign")

    _invoke_nook_patchapk(argv)
    if options.install:
        _adb_install(options.output_apk, usb=options.usb, serial=options.serial)
        if options.launch:
            if options.interaction == "connect":
                print("connect mode requires a live Nook server before launch", file=sys.stdout)
            _adb_launch(
                getattr(options, "package_name", ""),
                usb=options.usb,
                serial=options.serial,
            )
    return SimpleNamespace(output_apk=options.output_apk, repo_root=repo_root)
