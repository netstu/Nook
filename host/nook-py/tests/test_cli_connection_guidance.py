from io import StringIO
import os
import tempfile
import unittest

from nook import cli


class CliConnectionGuidanceTests(unittest.TestCase):
    def test_cli_reports_actionable_guidance_when_server_is_not_running(self):
        stderr = StringIO()
        stdout = StringIO()

        def failing_usb_device_factory(local_port: int, remote_port: int, timeout_ms: int, serial=None):
            raise ConnectionError("socket closed")

        exit_code = cli.main(
            argv=["ps", "--usb"],
            usb_device_factory=failing_usb_device_factory,
            stdout=stdout,
            stderr=stderr,
        )

        error_output = stderr.getvalue()

        self.assertEqual(exit_code, 1)
        self.assertIn("nook-server", error_output)
        self.assertIn("GitHub Release", error_output)
        self.assertIn("adb push", error_output)
        self.assertIn("su -c", error_output)

    def test_cli_rewrites_localized_windows_connection_refused_for_apps(self):
        stderr = StringIO()
        stdout = StringIO()

        def failing_usb_device_factory(local_port: int, remote_port: int, timeout_ms: int, serial=None):
            raise ConnectionRefusedError("[WinError 10061] 由于目标计算机积极拒绝，无法连接。")

        exit_code = cli.main(
            argv=["apps", "--usb"],
            usb_device_factory=failing_usb_device_factory,
            stdout=stdout,
            stderr=stderr,
        )

        error_output = stderr.getvalue()

        self.assertEqual(exit_code, 1)
        self.assertIn("nook-server", error_output)
        self.assertIn("GitHub Release", error_output)
        self.assertNotIn("Nook Gadget listen socket", error_output)

    def test_cli_reports_gadget_specific_guidance_when_gadget_attach_fails(self):
        stderr = StringIO()
        stdout = StringIO()
        stdin = StringIO("%exit\n")

        class AttachErrorDevice:
            def __init__(self) -> None:
                self.closed = False

            def close(self) -> None:
                self.closed = True

            def attach(self, target):
                raise ConnectionError("socket closed")

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('gadget-guidance')")
            script_path = handle.name

        try:
            exit_code = cli.main(
                argv=["-U", "--gadget", "com.demo.target", "-l", script_path],
                device_factory=lambda **kwargs: None,
                usb_device_factory=lambda **kwargs: AttachErrorDevice(),
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        error_output = stderr.getvalue()

        self.assertEqual(exit_code, 1)
        self.assertIn("Nook Gadget listen socket", error_output)
        self.assertIn("nook-cli -U --gadget com.demo.target -l hook.js", error_output)
        self.assertNotIn("nook-server", error_output)

    def test_rewrite_connection_error_does_not_use_gadget_guidance_for_non_attach_commands(self):
        message = cli._rewrite_connection_error(
            "[WinError 10061] 由于目标计算机积极拒绝，无法连接。",
            args=type(
                "Args",
                (),
                {
                    "command": "apps",
                    "gadget": True,
                },
            )(),
        )

        self.assertIn("nook-server", message)
        self.assertNotIn("Nook Gadget listen socket", message)


if __name__ == "__main__":
    unittest.main()
