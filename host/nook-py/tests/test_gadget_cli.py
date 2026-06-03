import os
import sys
import unittest
from pathlib import Path
import contextlib
import io
import tempfile
from unittest import mock


TEST_ROOT = os.path.dirname(__file__)
PACKAGE_ROOT = os.path.abspath(os.path.join(TEST_ROOT, ".."))
if PACKAGE_ROOT not in sys.path:
    sys.path.insert(0, PACKAGE_ROOT)


from nook import gadget_cli  # noqa: E402


class GadgetCliTests(unittest.TestCase):
    def test_patchapk_help_exposes_short_surface(self):
        parser = gadget_cli.build_parser()
        top_help = parser.format_help()
        self.assertIn("patchapk", top_help)
        self.assertIn("install", top_help)
        self.assertIn("launch", top_help)

        stdout = io.StringIO()
        with self.assertRaises(SystemExit), contextlib.redirect_stdout(stdout):
            parser.parse_args(["patchapk", "--help"])
        help_text = stdout.getvalue()

        self.assertIn("--source", help_text)
        self.assertIn("--output", help_text)
        self.assertIn("--architecture", help_text)
        self.assertIn("--startup-script", help_text)
        self.assertIn("--on-load", help_text)
        self.assertIn("--use-aapt2", help_text)

    def test_patchapk_defaults_match_repo_workflow(self):
        parser = gadget_cli.build_parser()
        args = parser.parse_args(["patchapk", "--source", "demo.apk"])

        self.assertEqual(args.command, "patchapk")
        self.assertEqual(args.source, "demo.apk")
        self.assertEqual(args.architecture, "arm64-v8a")
        self.assertEqual(args.bootstrap_mode, "minimal")
        self.assertEqual(args.startup_mode, "auto-start")
        self.assertEqual(args.on_load, "resume")
        self.assertEqual(args.decode_backend, "apktool")
        self.assertFalse(args.no_sign)
        self.assertFalse(args.use_aapt2)

    def test_default_output_path_uses_nook_suffix(self):
        output = gadget_cli.default_output_apk(Path("demo.apk"))
        self.assertEqual(output.name, "demo-nook.apk")

    def test_default_output_path_uses_repo_build_when_repo_root_provided(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo_root = Path(temp_dir)
            output = gadget_cli.default_output_apk(
                Path("demo.apk"),
                repo_root=repo_root,
            )
            self.assertEqual(
                output,
                repo_root / "build" / "demo-nook.apk",
            )

    def test_install_defaults_match_repo_workflow(self):
        parser = gadget_cli.build_parser()
        args = parser.parse_args(["install", "--apk", "demo-nook.apk"])

        self.assertEqual(args.command, "install")
        self.assertEqual(args.apk, "demo-nook.apk")
        self.assertFalse(args.no_reinstall)
        self.assertFalse(args.no_test)
        self.assertEqual(args.serial, "")

    def test_launch_supports_package_and_relative_activity(self):
        parser = gadget_cli.build_parser()
        args = parser.parse_args(
            [
                "launch",
                "--package",
                "com.demo.target",
                "--activity",
                ".MainActivity",
                "--stop-first",
                "--clear-logcat",
                "--wait",
                "3",
            ]
        )

        self.assertEqual(args.command, "launch")
        self.assertEqual(args.package, "com.demo.target")
        self.assertEqual(args.activity, ".MainActivity")
        self.assertTrue(args.stop_first)
        self.assertTrue(args.clear_logcat)
        self.assertEqual(args.wait, 3)

    def test_install_invokes_adb_install_with_repo_defaults(self):
        with mock.patch("nook.gadget_cli.subprocess.run") as run:
            exit_code = gadget_cli.main(["install", "--apk", "demo-nook.apk"])

        self.assertEqual(exit_code, 0)
        run.assert_called_once_with(
            ["adb", "install", "-r", "-t", "demo-nook.apk"],
            check=True,
        )

    def test_launch_invokes_force_stop_logcat_clear_and_start(self):
        with mock.patch("nook.gadget_cli.subprocess.run") as run, mock.patch(
            "nook.gadget_cli.time.sleep"
        ) as sleep:
            exit_code = gadget_cli.main(
                [
                    "launch",
                    "--package",
                    "com.demo.target",
                    "--activity",
                    ".MainActivity",
                    "--stop-first",
                    "--clear-logcat",
                    "--wait",
                    "2",
                ]
            )

        self.assertEqual(exit_code, 0)
        self.assertEqual(
            [call.args[0] for call in run.call_args_list],
            [
                ["adb", "shell", "am", "force-stop", "com.demo.target"],
                ["adb", "logcat", "-c"],
                ["adb", "shell", "am", "start", "-n", "com.demo.target/.MainActivity"],
            ],
        )
        sleep.assert_called_once_with(2)


if __name__ == "__main__":
    unittest.main()
