import os
import sys
import unittest
from types import SimpleNamespace
from unittest import mock


TEST_ROOT = os.path.dirname(__file__)
PACKAGE_ROOT = os.path.abspath(os.path.join(TEST_ROOT, ".."))
if PACKAGE_ROOT not in sys.path:
    sys.path.insert(0, PACKAGE_ROOT)


from nook import patchapk  # noqa: E402


class PatchApkWrapperTests(unittest.TestCase):
    def test_invoke_nook_patchapk_loads_tool_from_repo_root(self) -> None:
        fake_tool = mock.Mock()
        fake_tool.main.return_value = 0

        with mock.patch("nook.patchapk._load_patch_tool", return_value=fake_tool) as load_patch_tool:
            result = patchapk._invoke_nook_patchapk(["--print-plan"])

        self.assertEqual(result, 0)
        load_patch_tool.assert_called_once()
        fake_tool.main.assert_called_once_with(["--print-plan"])

    def test_invoke_nook_patchapk_exits_on_nonzero_tool_status(self) -> None:
        fake_tool = mock.Mock()
        fake_tool.main.return_value = 1

        with mock.patch("nook.patchapk._load_patch_tool", return_value=fake_tool):
            with self.assertRaises(SystemExit) as raised:
                patchapk._invoke_nook_patchapk(["--print-plan"])

        self.assertEqual(raised.exception.code, 1)

    def test_resolve_tool_uses_extra_candidates_after_path_lookup(self) -> None:
        with mock.patch("nook.patchapk._which", return_value=None), mock.patch(
            "nook.patchapk.os.path.isfile",
            side_effect=lambda path: path == r"E:\SDK\build-tools\34.0.0\apksigner.bat",
        ):
            resolved = patchapk._resolve_tool(
                "",
                "NOOK_APKSIGNER",
                "apksigner",
                "missing",
                extra_candidates=[
                    r"E:\SDK\build-tools\34.0.0\apksigner.bat",
                ],
            )

        self.assertEqual(resolved, r"E:\SDK\build-tools\34.0.0\apksigner.bat")

    def test_iter_android_build_tools_filenames_uses_sdk_roots(self) -> None:
        with mock.patch.dict(
            "nook.patchapk.os.environ",
            {"ANDROID_SDK_ROOT": r"E:\SDK"},
            clear=False,
        ), mock.patch("nook.patchapk.os.path.isdir", return_value=True), mock.patch(
            "nook.patchapk.os.listdir",
            return_value=["33.0.2", "34.0.0"],
        ):
            candidates = list(patchapk._iter_android_build_tools_filenames("apksigner.bat"))

        self.assertIn(r"E:\SDK\build-tools\34.0.0\apksigner.bat", candidates)
        self.assertIn(r"E:\SDK\build-tools\33.0.2\apksigner.bat", candidates)

    def test_listen_mode_emits_default_direct_attach_port(self) -> None:
        options = SimpleNamespace(
            input_apk="E:\\apps\\target.apk",
            output_apk="E:\\apps\\target-nook.apk",
            startup_script=None,
            bootstrap="minimal",
            startup_mode="auto-start",
            on_load="resume",
            interaction="listen",
            connect_host="127.0.0.1",
            connect_port=27042,
            listen_address="",
            listen_port=27042,
            decode_backend="apktool",
            sign=False,
            install=False,
            launch=False,
            usb=False,
            serial=None,
            apktool="apktool",
            apksigner="",
            zipalign="",
        )

        with mock.patch("nook.patchapk._resolve_tool", return_value="apktool"), mock.patch(
            "nook.patchapk._invoke_nook_patchapk"
        ) as invoke_patchapk:
            result = patchapk.run_patchapk(options)

        self.assertEqual(result.output_apk, options.output_apk)
        argv = invoke_patchapk.call_args.args[0]
        self.assertIn("--interaction-type", argv)
        self.assertIn("listen", argv)
        self.assertIn("--on-load", argv)
        self.assertIn("resume", argv)
        self.assertIn("--listen-port", argv)
        self.assertIn("27042", argv)
        self.assertNotIn("--connect-host", argv)
        self.assertNotIn("--connect-port", argv)
        self.assertIn("--no-sign", argv)

    def test_connect_mode_emits_outbound_endpoint(self) -> None:
        options = SimpleNamespace(
            input_apk="E:\\apps\\target.apk",
            output_apk="E:\\apps\\target-nook.apk",
            startup_script="E:\\scripts\\startup.js",
            bootstrap="minimal",
            startup_mode="manual",
            on_load="wait",
            interaction="connect",
            connect_host="10.0.2.2",
            connect_port=28042,
            listen_address="",
            listen_port=0,
            decode_backend="apktool",
            sign=False,
            install=False,
            launch=False,
            usb=False,
            serial=None,
            apktool="apktool",
            apksigner="",
            zipalign="",
        )

        with mock.patch("nook.patchapk._resolve_tool", return_value="apktool"), mock.patch(
            "nook.patchapk._invoke_nook_patchapk"
        ) as invoke_patchapk:
            patchapk.run_patchapk(options)

        argv = invoke_patchapk.call_args.args[0]
        self.assertIn("--interaction-type", argv)
        self.assertIn("connect", argv)
        self.assertIn("--on-load", argv)
        self.assertIn("wait", argv)
        self.assertIn("--connect-host", argv)
        self.assertIn("10.0.2.2", argv)
        self.assertIn("--connect-port", argv)
        self.assertIn("28042", argv)
        self.assertNotIn("--listen-port", argv)
        self.assertIn("--startup-script", argv)
        self.assertIn("E:\\scripts\\startup.js", argv)


if __name__ == "__main__":
    unittest.main()
