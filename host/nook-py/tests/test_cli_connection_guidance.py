from io import StringIO
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


if __name__ == "__main__":
    unittest.main()
