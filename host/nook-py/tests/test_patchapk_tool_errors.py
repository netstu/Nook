import io
import os
import sys
import subprocess
import unittest
from unittest import mock


REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
if REPO_ROOT not in sys.path:
    sys.path.insert(0, REPO_ROOT)


from tools import nook_patchapk  # noqa: E402


class PatchApkToolErrorTests(unittest.TestCase):
    def test_format_called_process_error_prefers_stderr(self) -> None:
        exc = subprocess.CalledProcessError(
            1,
            ["apktool", "b"],
            output="stdout line",
            stderr="stderr line",
        )

        self.assertEqual(
            nook_patchapk.format_called_process_error(exc),
            "stderr line",
        )

    def test_format_called_process_error_falls_back_to_stdout(self) -> None:
        exc = subprocess.CalledProcessError(
            1,
            ["apktool", "b"],
            output="stdout line",
            stderr="",
        )

        self.assertEqual(
            nook_patchapk.format_called_process_error(exc),
            "stdout line",
        )

    def test_main_reports_written_output_path_on_success(self) -> None:
        stdout = io.StringIO()
        with mock.patch(
            "tools.nook_patchapk.invoke_patch_apk_compat"
        ) as invoke_patch_apk_compat, mock.patch("sys.stdout", stdout):
            exit_code = nook_patchapk.main(
                [
                    "--input-apk",
                    "input.apk",
                    "--output-apk",
                    "output.apk",
                    "--gadget-lib",
                    "libnook-gadget.so",
                    "--no-sign",
                ]
            )

        self.assertEqual(exit_code, 0)
        invoke_patch_apk_compat.assert_called_once()
        self.assertIn("nook_patchapk wrote patched APK: output.apk", stdout.getvalue())

    def test_patch_apk_emits_stage_progress_for_internal_zip_mode(self) -> None:
        stdout = io.StringIO()
        with mock.patch("tools.nook_patchapk.unpack_apk_to_dir") as unpack_apk_to_dir, mock.patch(
            "tools.nook_patchapk.apply_patch_to_decoded_dir"
        ) as apply_patch_to_decoded_dir, mock.patch(
            "tools.nook_patchapk.rebuild_dir_to_apk"
        ) as rebuild_dir_to_apk, mock.patch("sys.stdout", stdout):
            nook_patchapk.patch_apk(
                input_apk="input.apk",
                output_apk="output.apk",
                abi="arm64-v8a",
                gadget_lib="libnook-gadget.so",
                startup_script=None,
                startup_mode="auto-start",
                on_load="resume",
                transport_mode="default",
                debug_logging=False,
                startup_script_required=False,
                no_sign=True,
                decode_backend="internal-zip",
                use_aapt2=False,
                apktool_path="apktool",
                jarsigner_path="jarsigner",
                apksigner_path="",
                zipalign_path="",
                keystore="",
                storepass="",
                key_alias="",
            )

        output = stdout.getvalue()
        self.assertIn("[nook-patchapk] [1/3] unpack apk", output)
        self.assertIn("[nook-patchapk] [2/3] patch package", output)
        self.assertIn("[nook-patchapk] [3/3] repack apk", output)
        unpack_apk_to_dir.assert_called_once()
        apply_patch_to_decoded_dir.assert_called_once()
        rebuild_dir_to_apk.assert_called_once()

    def test_run_progress_command_emits_heartbeat_for_long_running_stage(self) -> None:
        stdout = io.StringIO()
        process = mock.Mock()
        process.poll.side_effect = [None, None, 0]
        process.communicate.return_value = ("", "")
        process.returncode = 0

        with mock.patch("tools.nook_patchapk.subprocess.Popen", return_value=process), mock.patch(
            "tools.nook_patchapk.time.monotonic",
            side_effect=[0.0, 0.2, 5.4],
        ), mock.patch("tools.nook_patchapk.time.sleep"), mock.patch("sys.stdout", stdout):
            completed = nook_patchapk.run_progress_command(
                ["apktool", "b", "decoded", "-o", "out.apk"],
                progress_label="apktool build",
                heartbeat_interval_seconds=5.0,
                poll_interval_seconds=0.0,
            )

        self.assertEqual(completed.returncode, 0)
        self.assertIn(
            "[nook-patchapk] apktool build still running... 5s elapsed",
            stdout.getvalue(),
        )


if __name__ == "__main__":
    unittest.main()
