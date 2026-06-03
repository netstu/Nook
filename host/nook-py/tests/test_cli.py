import argparse
import io
import json
import os
import sys
import tempfile
import threading
from types import SimpleNamespace
import unittest
from unittest import mock


TEST_ROOT = os.path.dirname(__file__)
PACKAGE_ROOT = os.path.abspath(os.path.join(TEST_ROOT, ".."))
if PACKAGE_ROOT not in sys.path:
    sys.path.insert(0, PACKAGE_ROOT)


from nook.cli import _build_parser, _create_device, _format_repl_prompt, _normalize_dexdump_args, _normalize_sodump_args, _parse_repl_command, main  # noqa: E402
from nook.output import Console  # noqa: E402
from nook.protocol import AppEntry, ProcessEntry  # noqa: E402


class FakeScript:
    def __init__(self, source: str, name: str, call_order, create_error=None, load_error=None, unload_error=None) -> None:
        self.source = source
        self.name = name
        self._call_order = call_order
        self._create_error = create_error
        self._load_error = load_error
        self._unload_error = unload_error
        self.created = False
        self.loaded = False
        self.unloaded = False
        self.post_calls = []
        self.post_event = threading.Event()
        self.call_calls = []

    def create(self) -> int:
        self._call_order.append("script.create")
        if self._create_error is not None:
            raise self._create_error
        self.created = True
        return 1000

    def load(self):
        self._call_order.append("script.load")
        if self._load_error is not None:
            raise self._load_error
        self.loaded = True
        return None

    def unload(self):
        self._call_order.append("script.unload")
        if self._unload_error is not None:
            exc = self._unload_error
            self._unload_error = None
            raise exc
        self.unloaded = True
        return None

    def post(self, message: str, data: bytes = b"") -> None:
        self._call_order.append("script.post")
        self.post_calls.append((message, data))
        self.post_event.set()

    def call(self, method: str, *args, timeout_ms=None):
        self._call_order.append("script.call")
        self.call_calls.append((method, args, timeout_ms))
        return {"value": "pong", "args": list(args)}


class FakeSession:
    def __init__(self, pid: int, process_name: str, call_order, session_id: int = 1) -> None:
        self.pid = pid
        self.process_name = process_name
        self._call_order = call_order
        self.session_id = session_id
        self.created_scripts = []
        self.next_script_create_error = None
        self.next_script_load_error = None
        self.next_script_unload_error = None

    def create_script(self, source: str, name: str = "script.js") -> FakeScript:
        self._call_order.append("session.create_script")
        script = FakeScript(
            source=source,
            name=name,
            call_order=self._call_order,
            create_error=self.next_script_create_error,
            load_error=self.next_script_load_error,
            unload_error=self.next_script_unload_error,
        )
        self.next_script_unload_error = None
        self.created_scripts.append(script)
        return script


class FakeDevice:
    def __init__(self) -> None:
        self.call_order = []
        self.closed = False
        self.resume_calls = []
        self.spawn_calls = []
        self.attach_calls = []
        self.detach_calls = []
        self.post_calls = []
        self.wait_for_script_message_calls = []
        self.wait_for_script_message_results = []
        self.wait_for_script_message_default = TimeoutError()
        self.next_resume_error = None
        self.spawn_session = FakeSession(pid=4321, process_name="com.demo.target", call_order=self.call_order, session_id=10)
        self.attach_session = FakeSession(pid=2100, process_name="com.demo.target", call_order=self.call_order, session_id=7)

    def close(self) -> None:
        self.closed = True

    def enumerate_apps(self):
        return [
            AppEntry(package_name="com.android.systemui"),
            AppEntry(package_name="com.demo.target"),
        ]

    def enumerate_processes(self):
        return [
            ProcessEntry(pid=123, name="system_server"),
            ProcessEntry(pid=456, name="com.demo.target"),
        ]

    def spawn(self, identifier: str, argv=None, agent_ready_timeout_ms=None):
        self.call_order.append("spawn")
        self.spawn_calls.append((identifier, list(argv or []), agent_ready_timeout_ms))
        return self.spawn_session

    def attach(self, target):
        self.call_order.append("attach")
        self.attach_calls.append(target)
        return self.attach_session

    def detach(self, session_id: int):
        self.call_order.append("detach")
        self.detach_calls.append(session_id)

    def resume(self, pid: int):
        self.call_order.append("resume")
        if self.next_resume_error is not None:
            exc = self.next_resume_error
            self.next_resume_error = None
            raise exc
        self.resume_calls.append(pid)

    def post_script_message(self, script_id: int, message: str, data: bytes = b"") -> None:
        self.post_calls.append((script_id, message, data))

    def wait_for_script_message(self, timeout_ms=None, script_id=None):
        self.wait_for_script_message_calls.append((timeout_ms, script_id))
        if self.wait_for_script_message_results:
            result = self.wait_for_script_message_results.pop(0)
            if isinstance(result, BaseException):
                raise result
            return result
        result = self.wait_for_script_message_default
        if isinstance(result, BaseException):
            raise result
        return result


class ErrorDevice:
    def __init__(self, message: str) -> None:
        self._message = message

    def close(self) -> None:
        return None

    def enumerate_apps(self):
        raise RuntimeError(self._message)


class AttachErrorDevice(FakeDevice):
    def __init__(self, message: str) -> None:
        super().__init__()
        self._message = message

    def attach(self, target):
        self.call_order.append("attach")
        self.attach_calls.append(target)
        raise ConnectionError(self._message)


class CaptureStream:
    def __init__(self) -> None:
        self.parts = []

    def write(self, data: str) -> int:
        self.parts.append(data)
        return len(data)

    def flush(self) -> None:
        return None

    def isatty(self) -> bool:
        return False

    def get_captured(self) -> str:
        return "".join(self.parts)


class PromptRaceStream(CaptureStream):
    def __init__(self, console: Console, message_callback) -> None:
        super().__init__()
        self._console = console
        self._message_callback = message_callback
        self._triggered = False

    def flush(self) -> None:
        if not self._triggered:
            self._triggered = True
            self._message_callback()
        return None


class CliTests(unittest.TestCase):
    @staticmethod
    def _message(script_id: int, message: str, data: bytes = b""):
        class Message:
            def __init__(self):
                self.script_id = script_id
                self.message = message
                self.data = data

        return Message()

    def test_apps_command_uses_usb_factory_when_requested(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()
        captured = {}

        def usb_factory(**kwargs):
            captured.update(kwargs)
            return device

        exit_code = main(
            ["apps", "--usb", "--serial", "emulator-5554", "--port", "28042", "--timeout", "9000"],
            device_factory=lambda **kwargs: None,
            usb_device_factory=usb_factory,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(
            captured,
            {
                "local_port": 28042,
                "remote_port": 28042,
                "timeout_ms": 9000,
                "serial": "emulator-5554",
            },
        )
        self.assertIn("[*] Found 2 application(s)", stdout.getvalue())

    def test_apps_command_uses_usb_factory_when_requested_with_short_flag(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()
        captured = {}

        def usb_factory(**kwargs):
            captured.update(kwargs)
            return device

        exit_code = main(
            ["apps", "-U", "--serial", "emulator-5554", "--port", "28042", "--timeout", "9000"],
            device_factory=lambda **kwargs: None,
            usb_device_factory=usb_factory,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(
            captured,
            {
                "local_port": 28042,
                "remote_port": 28042,
                "timeout_ms": 9000,
                "serial": "emulator-5554",
            },
        )
        self.assertIn("[*] Found 2 application(s)", stdout.getvalue())

    def test_apps_command_accepts_short_usb_flag(self) -> None:
        parser = _build_parser()

        args = parser.parse_args(["apps", "-U"])

        self.assertEqual(args.command, "apps")
        self.assertTrue(args.usb)

    def test_ps_command_accepts_short_usb_flag(self) -> None:
        parser = _build_parser()

        args = parser.parse_args(["ps", "-U"])

        self.assertEqual(args.command, "ps")
        self.assertTrue(args.usb)

    def test_parser_supports_gadget_patchapk_passthrough(self) -> None:
        parser = _build_parser()

        args = parser.parse_args(
            [
                "gadget",
                "patchapk",
                "--source",
                "demo.apk",
                "--startup-script",
                "hook.js",
            ]
        )

        self.assertEqual(args.command, "gadget")
        self.assertEqual(
            args.gadget_argv,
            ["patchapk", "--source", "demo.apk", "--startup-script", "hook.js"],
        )

    def test_parser_supports_patchapk_minimal_defaults(self) -> None:
        parser = _build_parser()

        args = parser.parse_args(
            [
                "patchapk",
                "E:\\target.apk",
            ]
        )

        self.assertEqual(args.command, "patchapk")
        self.assertEqual(args.input_apk, "E:\\target.apk")
        self.assertIsNone(args.output_apk)
        self.assertIsNone(args.startup_script)
        self.assertEqual(args.bootstrap, "minimal")
        self.assertEqual(args.startup_mode, "auto-start")
        self.assertEqual(args.interaction, "listen")
        self.assertEqual(args.decode_backend, "apktool")
        self.assertTrue(args.sign)
        self.assertFalse(args.install)
        self.assertFalse(args.launch)
        self.assertFalse(args.usb)
        self.assertIsNone(args.serial)

    def test_parser_supports_patchapk_common_path_flags(self) -> None:
        parser = _build_parser()

        args = parser.parse_args(
            [
                "patchapk",
                "E:\\target.apk",
                "-o",
                "E:\\target-nook.apk",
                "-s",
                "startup.js",
                "--install",
                "--launch",
                "--usb",
                "--serial",
                "emulator-5554",
            ]
        )

        self.assertEqual(args.command, "patchapk")
        self.assertEqual(args.input_apk, "E:\\target.apk")
        self.assertEqual(args.output_apk, "E:\\target-nook.apk")
        self.assertEqual(args.startup_script, "startup.js")
        self.assertTrue(args.install)
        self.assertTrue(args.launch)
        self.assertTrue(args.usb)
        self.assertEqual(args.serial, "emulator-5554")

    def test_parser_supports_dexdump_attach_mode(self) -> None:
        parser = _build_parser()

        args = parser.parse_args(
            [
                "dexdump",
                "com.demo.target",
                "-U",
                "-o",
                "E:\\dump\\target",
                "--deep",
                "--sleep",
                "3000",
            ]
        )

        self.assertEqual(args.command, "dexdump")
        self.assertEqual(args.target, "com.demo.target")
        self.assertEqual(args.output, "E:\\dump\\target")
        self.assertTrue(args.deep)
        self.assertEqual(args.sleep, 3000)
        self.assertTrue(args.usb)
        self.assertIsNone(args.spawn_package)

    def test_parser_supports_dexdump_spawn_mode(self) -> None:
        parser = _build_parser()

        args = parser.parse_args(
            [
                "dexdump",
                "--spawn",
                "com.demo.target",
                "--json",
            ]
        )

        self.assertEqual(args.command, "dexdump")
        self.assertEqual(args.spawn_package, "com.demo.target")
        self.assertTrue(args.json)
        self.assertIsNone(args.target)

    def test_patchapk_command_maps_minimal_defaults_into_patch_engine_wrapper(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()
        calls = []

        def fake_patchapk_runner(options):
            calls.append(options)
            return SimpleNamespace(output_apk=options.output_apk)

        with mock.patch("nook.cli._run_patchapk", side_effect=fake_patchapk_runner):
            exit_code = main(
                [
                    "patchapk",
                    "E:\\apps\\target.apk",
                ],
                stdout=stdout,
                stderr=stderr,
            )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stdout.getvalue(), "")
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(len(calls), 1)
        options = calls[0]
        self.assertEqual(options.input_apk, "E:\\apps\\target.apk")
        self.assertEqual(options.output_apk, "E:\\apps\\target-nook.apk")
        self.assertIsNone(options.startup_script)
        self.assertEqual(options.bootstrap, "minimal")
        self.assertEqual(options.startup_mode, "auto-start")
        self.assertEqual(options.interaction, "listen")
        self.assertEqual(options.connect_host, "127.0.0.1")
        self.assertEqual(options.connect_port, 27042)
        self.assertEqual(options.listen_address, "")
        self.assertEqual(options.listen_port, 27042)
        self.assertEqual(options.decode_backend, "apktool")
        self.assertTrue(options.sign)
        self.assertFalse(options.install)
        self.assertFalse(options.launch)
        self.assertFalse(options.usb)
        self.assertIsNone(options.serial)

    def test_patchapk_command_maps_explicit_overrides_into_patch_engine_wrapper(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()
        calls = []

        def fake_patchapk_runner(options):
            calls.append(options)
            return SimpleNamespace(output_apk=options.output_apk)

        with mock.patch("nook.cli._run_patchapk", side_effect=fake_patchapk_runner):
            exit_code = main(
                [
                    "patchapk",
                    "E:\\apps\\target.apk",
                    "-o",
                    "E:\\out\\patched.apk",
                    "-s",
                    "E:\\scripts\\startup.js",
                    "--bootstrap",
                    "minimal",
                    "--startup-mode",
                    "manual",
                    "--interaction",
                    "connect",
                    "--connect-host",
                    "10.0.2.2",
                    "--connect-port",
                    "28042",
                    "--decode-backend",
                    "internal-zip",
                    "--no-sign",
                    "--install",
                    "--launch",
                    "--usb",
                    "--serial",
                    "emulator-5554",
                ],
                stdout=stdout,
                stderr=stderr,
            )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stdout.getvalue(), "")
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(len(calls), 1)
        options = calls[0]
        self.assertEqual(options.input_apk, "E:\\apps\\target.apk")
        self.assertEqual(options.output_apk, "E:\\out\\patched.apk")
        self.assertEqual(options.startup_script, "E:\\scripts\\startup.js")
        self.assertEqual(options.bootstrap, "minimal")
        self.assertEqual(options.startup_mode, "manual")
        self.assertEqual(options.interaction, "connect")
        self.assertEqual(options.connect_host, "10.0.2.2")
        self.assertEqual(options.connect_port, 28042)
        self.assertEqual(options.listen_address, "")
        self.assertEqual(options.listen_port, 0)
        self.assertEqual(options.decode_backend, "internal-zip")
        self.assertFalse(options.sign)
        self.assertTrue(options.install)
        self.assertTrue(options.launch)
        self.assertTrue(options.usb)
        self.assertEqual(options.serial, "emulator-5554")

    def test_patchapk_command_allows_explicit_listen_socket_overrides(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()
        calls = []

        def fake_patchapk_runner(options):
            calls.append(options)
            return SimpleNamespace(output_apk=options.output_apk)

        with mock.patch("nook.cli._run_patchapk", side_effect=fake_patchapk_runner):
            exit_code = main(
                [
                    "patchapk",
                    "E:\\apps\\target.apk",
                    "--interaction",
                    "listen",
                    "--listen-address",
                    "0.0.0.0",
                    "--listen-port",
                    "29042",
                ],
                stdout=stdout,
                stderr=stderr,
            )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(len(calls), 1)
        options = calls[0]
        self.assertEqual(options.interaction, "listen")
        self.assertEqual(options.listen_address, "0.0.0.0")
        self.assertEqual(options.listen_port, 29042)

    def test_create_device_prefers_usb_gadget_localabstract_socket_for_attach_targets(self) -> None:
        args = argparse.Namespace(
            command="attach",
            repl_mode=None,
            usb=True,
            serial="21ce24db",
            port=27042,
            timeout=5000,
            target="com.demo.target",
            attach=False,
            spawn=False,
        )
        calls = []
        expected_device = object()

        def usb_factory(**kwargs):
            calls.append(kwargs)
            if len(calls) == 1:
                raise ConnectionError("socket closed")
            return expected_device

        device = _create_device(
            args,
            device_factory=lambda **kwargs: None,
            usb_device_factory=usb_factory,
        )

        self.assertIs(device, expected_device)
        self.assertEqual(calls[0]["remote_abstract"], "nook-gadget-com.demo.target")
        self.assertEqual(calls[1]["remote_port"], 27042)
        self.assertEqual(calls[1].get("remote_abstract", ""), "")

    def test_parser_supports_repl_spawn(self) -> None:
        parser = _build_parser()

        args = parser.parse_args(
            [
                "repl",
                "spawn",
                "com.demo.target",
                "-l",
                "hook.js",
                "--resume",
                "--usb",
            ]
        )

        self.assertEqual(args.command, "repl")
        self.assertEqual(args.repl_mode, "spawn")
        self.assertEqual(args.package, "com.demo.target")
        self.assertEqual(args.script_path, "hook.js")
        self.assertTrue(args.resume)
        self.assertTrue(args.usb)

    def test_parser_supports_repl_attach(self) -> None:
        parser = _build_parser()

        args = parser.parse_args(
            [
                "repl",
                "attach",
                "com.demo.target",
                "-l",
                "hook.js",
                "--usb",
            ]
        )

        self.assertEqual(args.command, "repl")
        self.assertEqual(args.repl_mode, "attach")
        self.assertEqual(args.target, "com.demo.target")
        self.assertEqual(args.script_path, "hook.js")
        self.assertFalse(hasattr(args, "resume"))
        self.assertTrue(args.usb)

    def test_normalize_dexdump_args_raises_default_message_timeout_floor(self) -> None:
        args = argparse.Namespace(
            target="com.demo.target",
            spawn_package=None,
            output=None,
            deep=False,
            sleep=0,
            fix_header=False,
            dedupe="md5",
            agent_ready_timeout=10000,
            message_timeout=None,
            max_results=64,
            min_dex_size=0x70,
            max_dex_size=64 * 1024 * 1024,
            scan_window_ranges=8,
            include_system=False,
            debug=False,
            force_chunk_scan=True,
            json=False,
            timeout=15000,
        )

        options = _normalize_dexdump_args(args)

        self.assertEqual(options.message_timeout, 60000)
        self.assertTrue(options.force_chunk_scan)

    def test_normalize_dexdump_args_uses_conservative_unlimited_scan_defaults(self) -> None:
        args = argparse.Namespace(
            target="com.demo.target",
            spawn_package=None,
            output=None,
            deep=False,
            sleep=0,
            fix_header=False,
            dedupe="md5",
            agent_ready_timeout=10000,
            message_timeout=None,
            max_results=0,
            min_dex_size=0x70,
            max_dex_size=64 * 1024 * 1024,
            scan_window_ranges=8,
            include_system=False,
            debug=False,
            force_chunk_scan=False,
            json=False,
            timeout=15000,
        )

        options = _normalize_dexdump_args(args)

        self.assertEqual(options.max_results, 0)
        self.assertEqual(options.scan_window_ranges, 8)
        self.assertEqual(options.scan_window_max_bytes, 64 * 1024 * 1024)
        self.assertEqual(options.scan_slice_bytes, 8 * 1024 * 1024)
        self.assertFalse(options.isolate_raw_anonymous)
        self.assertTrue(options.fast_full_scan)

    def test_normalize_dexdump_args_preserves_requested_window_for_limited_scan(self) -> None:
        args = argparse.Namespace(
            target="com.demo.target",
            spawn_package=None,
            output=None,
            deep=False,
            sleep=0,
            fix_header=False,
            dedupe="md5",
            agent_ready_timeout=10000,
            message_timeout=None,
            max_results=8,
            min_dex_size=0x70,
            max_dex_size=64 * 1024 * 1024,
            scan_window_ranges=8,
            include_system=False,
            debug=False,
            force_chunk_scan=False,
            json=False,
            timeout=15000,
        )

        options = _normalize_dexdump_args(args)

        self.assertEqual(options.scan_window_ranges, 8)
        self.assertIsNone(options.scan_window_max_bytes)
        self.assertIsNone(options.scan_slice_bytes)
        self.assertIsNone(options.isolate_raw_anonymous)
        self.assertTrue(options.fast_full_scan)

    def test_normalize_sodump_args_inherits_transport_timeout_for_message_timeout(self) -> None:
        args = argparse.Namespace(
            target="com.demo.target",
            spawn_package=None,
            module="libdemo.so",
            base_so=None,
            output=None,
            sleep=0,
            fix=True,
            agent_ready_timeout=10000,
            message_timeout=None,
            debug=False,
            json=False,
            timeout=15000,
        )

        options = _normalize_sodump_args(args)

        self.assertEqual(options.message_timeout, 15000)

    def test_parser_supports_frida_style_top_level_spawn_with_script_enters_repl(self) -> None:
        parser = _build_parser()

        args = parser.parse_args(
            [
                "-U",
                "-f",
                "com.demo.target",
                "-l",
                "hook.js",
            ]
        )

        self.assertEqual(args.command, "repl")
        self.assertEqual(args.repl_mode, "spawn")
        self.assertEqual(args.package, "com.demo.target")
        self.assertEqual(args.script_path, "hook.js")
        self.assertTrue(args.usb)
        self.assertTrue(args.resume)

    def test_parser_supports_frida_style_top_level_attach_with_name(self) -> None:
        parser = _build_parser()

        args = parser.parse_args(
            [
                "-U",
                "-n",
                "com.demo.target",
                "-l",
                "hook.js",
            ]
        )

        self.assertEqual(args.command, "repl")
        self.assertEqual(args.repl_mode, "attach")
        self.assertEqual(args.target, "com.demo.target")
        self.assertEqual(args.script_path, "hook.js")
        self.assertTrue(args.usb)

    def test_parser_supports_frida_style_top_level_attach_with_gadget_flag(self) -> None:
        parser = _build_parser()

        args = parser.parse_args(
            [
                "-U",
                "--gadget",
                "com.demo.target",
                "-l",
                "hook.js",
            ]
        )

        self.assertEqual(args.command, "repl")
        self.assertEqual(args.repl_mode, "attach")
        self.assertEqual(args.target, "com.demo.target")
        self.assertEqual(args.script_path, "hook.js")
        self.assertTrue(args.usb)
        self.assertTrue(args.gadget)

    def test_parser_supports_frida_style_top_level_attach_with_identifier(self) -> None:
        parser = _build_parser()

        args = parser.parse_args(
            [
                "-U",
                "-N",
                "com.demo.target",
                "-l",
                "hook.js",
            ]
        )

        self.assertEqual(args.command, "repl")
        self.assertEqual(args.repl_mode, "attach")
        self.assertEqual(args.target, "com.demo.target")
        self.assertEqual(args.script_path, "hook.js")
        self.assertTrue(args.usb)

    def test_parser_supports_frida_style_top_level_attach_with_pid(self) -> None:
        parser = _build_parser()

        args = parser.parse_args(
            [
                "-U",
                "-p",
                "4321",
                "-l",
                "hook.js",
            ]
        )

        self.assertEqual(args.command, "repl")
        self.assertEqual(args.repl_mode, "attach")
        self.assertEqual(args.target, 4321)
        self.assertEqual(args.script_path, "hook.js")
        self.assertTrue(args.usb)

    def test_parser_supports_symbi_for_spawn(self) -> None:
        parser = _build_parser()

        args = parser.parse_args(
            [
                "spawn",
                "com.demo.target",
                "--symbi",
            ]
        )

        self.assertEqual(args.command, "spawn")
        self.assertEqual(args.package, "com.demo.target")
        self.assertTrue(args.spawn_symbi)

    def test_parser_supports_symbi_for_frida_style_spawn(self) -> None:
        parser = _build_parser()

        args = parser.parse_args(
            [
                "-U",
                "-f",
                "com.demo.target",
                "--symbi",
            ]
        )

        self.assertEqual(args.command, "repl")
        self.assertEqual(args.repl_mode, "spawn")
        self.assertEqual(args.package, "com.demo.target")
        self.assertTrue(args.spawn_symbi)

    def test_parser_supports_strict_zygote_control_for_spawn(self) -> None:
        parser = _build_parser()

        args = parser.parse_args(
            [
                "spawn",
                "com.demo.target",
                "--strict-zygote-control",
            ]
        )

        self.assertEqual(args.command, "spawn")
        self.assertEqual(args.package, "com.demo.target")
        self.assertTrue(args.strict_zygote_control)

    def test_parser_supports_strict_zygote_control_for_frida_style_spawn(self) -> None:
        parser = _build_parser()

        args = parser.parse_args(
            [
                "-U",
                "-f",
                "com.demo.target",
                "--strict-zygote-control",
            ]
        )

        self.assertEqual(args.command, "repl")
        self.assertEqual(args.repl_mode, "spawn")
        self.assertEqual(args.package, "com.demo.target")
        self.assertTrue(args.strict_zygote_control)

    def test_parser_supports_frida_style_top_level_attach(self) -> None:
        parser = _build_parser()

        args = parser.parse_args(
            [
                "-U",
                "com.demo.target",
                "-l",
                "hook.js",
            ]
        )

        self.assertEqual(args.command, "repl")
        self.assertEqual(args.repl_mode, "attach")
        self.assertEqual(args.target, "com.demo.target")
        self.assertEqual(args.script_path, "hook.js")
        self.assertTrue(args.usb)

    def test_main_rejects_symbi_for_frida_style_top_level_attach(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()

        exit_code = main(
            [
                "-U",
                "com.demo.target",
                "-l",
                "hook.js",
                "--symbi",
            ],
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 1)
        self.assertEqual(stdout.getvalue(), "")
        self.assertIn("--symbi is only valid for spawn", stderr.getvalue())

    def test_main_rejects_spawn_backend_flags_for_call_attach(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()

        exit_code = main(
            [
                "call",
                "com.demo.target",
                "-l",
                "hook.js",
                "ping",
                "--attach",
                "--strict-zygote-control",
            ],
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 1)
        self.assertEqual(stdout.getvalue(), "")
        self.assertIn("--strict-zygote-control is only valid for spawn", stderr.getvalue())

    def test_top_level_help_mentions_frida_style_invocations(self) -> None:
        help_text = _build_parser().format_help()

        self.assertIn("nook-cli -U -f com.demo.target -l hook.js", help_text)
        self.assertIn("Experimental spawn routing:", help_text)
        self.assertIn("nook-cli -U -f com.demo.target -l hook.js --strict-zygote-control", help_text)
        self.assertIn("nook-cli -U -f com.demo.target -l hook.js --symbi", help_text)
        self.assertIn("nook-cli -U com.demo.target -l hook.js", help_text)
        self.assertIn("nook-cli -U -n com.demo.target -l hook.js", help_text)
        self.assertIn("nook-cli -U -N com.demo.target -l hook.js", help_text)
        self.assertIn("nook-cli -U -p 4321 -l hook.js", help_text)
        self.assertIn("nook-cli patchapk target.apk -s startup.js", help_text)
        self.assertIn("nook-cli patchapk target.apk -o target-patched.apk --install --launch --usb", help_text)
        self.assertIn("{apps,ps,spawn,attach,call,detach,resume,post,unload,repl,patchapk,gadget,dexdump,sodump}", help_text)

    def test_main_help_prints_frida_style_invocations(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()

        exit_code = main(["--help"], stdout=stdout, stderr=stderr)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertIn("nook-cli -U -f com.demo.target -l hook.js", stdout.getvalue())
        self.assertIn("Experimental spawn routing:", stdout.getvalue())
        self.assertIn("nook-cli -U -f com.demo.target -l hook.js --strict-zygote-control", stdout.getvalue())
        self.assertIn("nook-cli -U -f com.demo.target -l hook.js --symbi", stdout.getvalue())
        self.assertIn("nook-cli -U com.demo.target -l hook.js", stdout.getvalue())
        self.assertIn("nook-cli -U -n com.demo.target -l hook.js", stdout.getvalue())
        self.assertIn("nook-cli -U -N com.demo.target -l hook.js", stdout.getvalue())
        self.assertIn("nook-cli -U -p 4321 -l hook.js", stdout.getvalue())
        self.assertIn("nook-cli patchapk target.apk -s startup.js", stdout.getvalue())
        self.assertIn("nook-cli patchapk target.apk -o target-patched.apk --install --launch --usb", stdout.getvalue())

    def test_main_dispatches_gadget_subcommand(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()

        with mock.patch("nook.cli._run_gadget_cli", return_value=0) as run_gadget_cli:
            exit_code = main(
                [
                    "gadget",
                    "patchapk",
                    "--source",
                    "demo.apk",
                    "--print-cmd",
                ],
                stdout=stdout,
                stderr=stderr,
            )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stdout.getvalue(), "")
        self.assertEqual(stderr.getvalue(), "")
        run_gadget_cli.assert_called_once_with(
            ["patchapk", "--source", "demo.apk", "--print-cmd"]
        )

    def test_main_dispatches_patchapk_subcommand(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()

        with mock.patch("nook.cli._run_patchapk", return_value=SimpleNamespace(output_apk="demo-nook.apk")) as run_patchapk:
            exit_code = main(
                [
                    "patchapk",
                    "demo.apk",
                    "-s",
                    "startup.js",
                ],
                stdout=stdout,
                stderr=stderr,
            )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stdout.getvalue(), "")
        self.assertEqual(stderr.getvalue(), "")
        run_patchapk.assert_called_once()

    def test_main_dispatches_dexdump_subcommand(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()

        with mock.patch("nook.cli._run_dexdump", return_value={"artifact_count": 1}) as run_dexdump:
            exit_code = main(
                [
                    "dexdump",
                    "com.demo.target",
                    "-o",
                    "E:\\dump\\target",
                ],
                stdout=stdout,
                stderr=stderr,
            )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stdout.getvalue(), "")
        self.assertEqual(stderr.getvalue(), "")
        run_dexdump.assert_called_once()

    def test_main_dispatches_sodump_subcommand(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()

        with mock.patch("nook.cli._run_sodump", return_value={"ok": True, "artifact_count": 1}) as run_sodump:
            exit_code = main(
                [
                    "sodump",
                    "com.demo.target",
                    "-U",
                    "--module",
                    "libfoo.so",
                ],
                stdout=stdout,
                stderr=stderr,
            )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stdout.getvalue(), "")
        self.assertEqual(stderr.getvalue(), "")
        run_sodump.assert_called_once()

    def test_normalize_sodump_args_accepts_base_so(self) -> None:
        parser = _build_parser()
        args = parser.parse_args(
            [
                "sodump",
                "com.demo.target",
                "--module",
                "libfoo.so",
                "--base-so",
                "E:\\ref\\libfoo.so",
            ]
        )

        options = _normalize_sodump_args(args)

        self.assertEqual(options.base_so, "E:\\ref\\libfoo.so")

    def test_main_returns_failure_for_sodump_partial_failure(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()

        with mock.patch("nook.cli._run_sodump", return_value={"ok": False, "artifact_count": 1}) as run_sodump:
            exit_code = main(
                [
                    "sodump",
                    "com.demo.target",
                    "--module",
                    "libfoo.so",
                ],
                stdout=stdout,
                stderr=stderr,
            )

        self.assertEqual(exit_code, 1)
        run_sodump.assert_called_once()

    def test_frida_style_help_marks_experimental_spawn_flags(self) -> None:
        parser = _build_parser()
        frida_help = parser._frida_parser.format_help()  # type: ignore[attr-defined]

        self.assertIn("experimental: prefer the symbi spawn backend", frida_help)
        self.assertIn("experimental: attempt zygote-control first", frida_help)

    def test_format_repl_prompt_without_script(self) -> None:
        context = SimpleNamespace(
            session=SimpleNamespace(pid=4321, process_name="com.demo.target"),
            script_name=None,
            resumed=True,
        )

        self.assertEqual(_format_repl_prompt(context), "[Local::com.demo.target]-> ")

    def test_format_repl_prompt_with_script_and_suspended_state(self) -> None:
        context = SimpleNamespace(
            session=SimpleNamespace(pid=4321, process_name="com.demo.target"),
            script_name="hook.js",
            resumed=False,
        )

        self.assertEqual(
            _format_repl_prompt(context),
            "[Local::com.demo.target] (suspended)-> ",
        )

    def test_parse_repl_command_for_call(self) -> None:
        command = _parse_repl_command('%call ping ["hello"]')

        self.assertEqual(
            command,
            {
                "name": "call",
                "args": ["ping", '["hello"]'],
                "raw": '%call ping ["hello"]',
            },
        )

    def test_parse_repl_command_supports_slash_prefix(self) -> None:
        command = _parse_repl_command('/call ping ["hello"]')

        self.assertEqual(
            command,
            {
                "name": "call",
                "args": ["ping", '["hello"]'],
                "raw": '/call ping ["hello"]',
            },
        )

    def test_parse_repl_command_treats_plain_text_as_post(self) -> None:
        command = _parse_repl_command("hello-from-stdin")

        self.assertEqual(
            command,
            {
                "name": "post",
                "args": ["hello-from-stdin"],
                "raw": "hello-from-stdin",
            },
        )

    def test_parse_repl_command_ignores_blank_input(self) -> None:
        self.assertIsNone(_parse_repl_command("   "))

    def test_apps_command_prints_apps(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        exit_code = main(
            ["apps"],
            device_factory=lambda **kwargs: device,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertIn("[*] Found 2 application(s)", stdout.getvalue())
        self.assertIn("PID", stdout.getvalue())
        self.assertIn("Identifier", stdout.getvalue())
        self.assertIn("com.android.systemui", stdout.getvalue())
        self.assertIn("com.demo.target", stdout.getvalue())

    def test_apps_command_can_emit_json(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        exit_code = main(
            ["apps", "--json"],
            device_factory=lambda **kwargs: device,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        payload = json.loads(stdout.getvalue())
        self.assertEqual(
            payload,
            {
                "ok": True,
                "count": 2,
                "apps": ["com.android.systemui", "com.demo.target"],
            },
        )

    def test_ps_command_prints_processes(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        exit_code = main(
            ["ps"],
            device_factory=lambda **kwargs: device,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertIn("[*] Found 2 process(es)", stdout.getvalue())
        self.assertIn("PID", stdout.getvalue())
        self.assertIn("Name", stdout.getvalue())
        self.assertIn("123", stdout.getvalue())
        self.assertIn("system_server", stdout.getvalue())
        self.assertIn("456", stdout.getvalue())
        self.assertIn("com.demo.target", stdout.getvalue())

    def test_repl_spawn_bootstraps_loads_script_before_resume_and_exits(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()
        stdin = io.StringIO("%exit\n")

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('repl-spawn')")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "repl",
                    "spawn",
                    "com.demo.target",
                    "-l",
                    script_path,
                    "--resume",
                    "--agent-ready-timeout",
                    "9000",
                ],
                device_factory=lambda **kwargs: device,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(device.spawn_calls, [("com.demo.target", [], 9000)])
        self.assertEqual(device.resume_calls, [4321])
        self.assertEqual(
            device.call_order[:5],
            ["spawn", "session.create_script", "script.create", "script.load", "resume"],
        )
        self.assertTrue(device.closed)

    def test_spawn_command_with_symbi_passes_internal_marker(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        exit_code = main(
            [
                "spawn",
                "com.demo.target",
                "--oneshot",
                "--symbi",
            ],
            device_factory=lambda **kwargs: device,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(
            device.spawn_calls,
            [("com.demo.target", ["--nook-spawn-backend=symbi"], 10000)],
        )

    def test_spawn_command_default_path_does_not_pass_internal_symbi_marker(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        exit_code = main(
            [
                "spawn",
                "com.demo.target",
                "--oneshot",
            ],
            device_factory=lambda **kwargs: device,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(
            device.spawn_calls,
            [("com.demo.target", [], 10000)],
        )

    def test_spawn_command_prints_banner_for_human_output(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        exit_code = main(
            [
                "spawn",
                "com.demo.target",
                "--oneshot",
            ],
            device_factory=lambda **kwargs: device,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        output = stdout.getvalue()
        self.assertIn("Dynamic instrumentation toolkit for Android", output)
        self.assertIn("[*] Spawning 'com.demo.target'...", output)

    def test_spawn_command_with_strict_zygote_control_passes_internal_marker(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        exit_code = main(
            [
                "spawn",
                "com.demo.target",
                "--strict-zygote-control",
                "--oneshot",
            ],
            device_factory=lambda **kwargs: device,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(
            device.spawn_calls,
            [("com.demo.target", ["--nook-strict-zygote-control"], 10000)],
        )

    def test_frida_style_spawn_with_symbi_passes_internal_marker(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()
        stdin = io.StringIO("%exit\n")

        exit_code = main(
            [
                "-U",
                "-f",
                "com.demo.target",
                "--symbi",
            ],
            device_factory=lambda **kwargs: device,
            usb_device_factory=lambda **kwargs: device,
            stdin=stdin,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(
            device.spawn_calls,
            [("com.demo.target", ["--nook-spawn-backend=symbi"], 10000)],
        )

    def test_frida_style_spawn_with_strict_zygote_control_passes_internal_marker(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()
        stdin = io.StringIO("%exit\n")

        exit_code = main(
            [
                "-U",
                "-f",
                "com.demo.target",
                "--strict-zygote-control",
            ],
            device_factory=lambda **kwargs: device,
            usb_device_factory=lambda **kwargs: device,
            stdin=stdin,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(
            device.spawn_calls,
            [("com.demo.target", ["--nook-strict-zygote-control"], 10000)],
        )

    def test_frida_style_top_level_spawn_with_script_enters_interactive_session(self) -> None:
        device = FakeDevice()
        device.wait_for_script_message_results = [
            CliTests._message(1000, '{"type":"send","payload":"frida-style-spawn"}'),
            KeyboardInterrupt(),
        ]
        stdout = io.StringIO()
        stderr = io.StringIO()
        stdin = io.StringIO("%exit\n")

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('frida-style-spawn')")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "-U",
                    "-f",
                    "com.demo.target",
                    "-l",
                    script_path,
                ],
                device_factory=lambda **kwargs: device,
                usb_device_factory=lambda **kwargs: device,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(device.spawn_calls, [("com.demo.target", [], 10000)])
        self.assertEqual(device.resume_calls, [4321])
        self.assertEqual(
            device.wait_for_script_message_calls,
            [(1000, 1000), (1000, 1000)],
        )
        self.assertEqual(
            device.call_order[:5],
            ["spawn", "session.create_script", "script.create", "script.load", "resume"],
        )
        self.assertIn("script.unload", device.call_order)
        script = device.spawn_session.created_scripts[0]
        self.assertTrue(script.unloaded)
        output = stdout.getvalue()
        banner_index = output.index("Dynamic instrumentation toolkit for Android")
        loading_index = output.index("[*] Loading")
        resuming_index = output.index("[*] Resuming")
        self.assertLess(banner_index, loading_index)
        self.assertLess(banner_index, resuming_index)
        self.assertEqual(output.count("Dynamic instrumentation toolkit for Android"), 1)
        self.assertIn("[*] Loading '%s'..." % os.path.basename(script_path), output)
        self.assertIn("[USB Device::com.demo.target] frida-style-spawn", output)
        self.assertIn("[+] Script unloaded (id: 1000)", output)

    def test_frida_style_top_level_attach_enters_interactive_session(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()
        stdin = io.StringIO("%exit\n")

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('frida-style-attach')")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "-U",
                    "com.demo.target",
                    "-l",
                    script_path,
                ],
                device_factory=lambda **kwargs: device,
                usb_device_factory=lambda **kwargs: device,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(device.attach_calls, ["com.demo.target"])
        self.assertEqual(device.resume_calls, [])
        self.assertEqual(device.call_order[:4], ["attach", "session.create_script", "script.create", "script.load"])
        output = stdout.getvalue()
        banner_index = output.index("Dynamic instrumentation toolkit for Android")
        loading_index = output.index("[*] Loading")
        self.assertLess(banner_index, loading_index)
        self.assertEqual(output.count("Dynamic instrumentation toolkit for Android"), 1)
        self.assertIn("[*] Loading '%s'..." % os.path.basename(script_path), output)

    def test_frida_style_top_level_attach_retries_via_tcp_after_gadget_socket_close(self) -> None:
        first_device = AttachErrorDevice("socket closed")
        second_device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()
        stdin = io.StringIO("%exit\n")
        usb_calls = []

        def usb_factory(**kwargs):
            usb_calls.append(kwargs)
            if kwargs.get("remote_abstract", ""):
                return first_device
            return second_device

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('frida-style-attach-fallback')")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "-U",
                    "com.demo.target",
                    "-l",
                    script_path,
                ],
                device_factory=lambda **kwargs: None,
                usb_device_factory=usb_factory,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertTrue(first_device.closed)
        self.assertEqual(first_device.attach_calls, ["com.demo.target"])
        self.assertEqual(second_device.attach_calls, ["com.demo.target"])
        self.assertEqual(usb_calls[0]["remote_abstract"], "nook-gadget-com.demo.target")
        self.assertEqual(usb_calls[1].get("remote_abstract", ""), "")

    def test_frida_style_top_level_attach_with_gadget_flag_does_not_fallback_to_tcp(self) -> None:
        first_device = AttachErrorDevice("socket closed")
        stdout = io.StringIO()
        stderr = io.StringIO()
        stdin = io.StringIO("%exit\n")
        usb_calls = []

        def usb_factory(**kwargs):
            usb_calls.append(kwargs)
            return first_device

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('frida-style-attach-gadget-only')")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "-U",
                    "--gadget",
                    "com.demo.target",
                    "-l",
                    script_path,
                ],
                device_factory=lambda **kwargs: None,
                usb_device_factory=usb_factory,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 1)
        self.assertEqual(first_device.attach_calls, ["com.demo.target"])
        self.assertEqual(len(usb_calls), 1)
        self.assertEqual(usb_calls[0]["remote_abstract"], "nook-gadget-com.demo.target")
        self.assertEqual(usb_calls[0].get("remote_port"), 27042)
        self.assertEqual(stdout.getvalue().count("Dynamic instrumentation toolkit for Android"), 1)

    def test_frida_style_top_level_attach_retries_via_gadget_socket_after_tcp_socket_close(self) -> None:
        first_device = AttachErrorDevice("socket closed")
        second_device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()
        stdin = io.StringIO("%exit\n")
        usb_calls = []

        def usb_factory(**kwargs):
            usb_calls.append(kwargs)
            if kwargs.get("remote_abstract", ""):
                if len([call for call in usb_calls if call.get("remote_abstract", "")]) == 1:
                    raise ConnectionError("socket closed")
                return second_device
            if len([call for call in usb_calls if not call.get("remote_abstract", "")]) == 1:
                return first_device
            return second_device

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('frida-style-attach-fallback')")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "-U",
                    "com.demo.target",
                    "-l",
                    script_path,
                ],
                device_factory=lambda **kwargs: None,
                usb_device_factory=usb_factory,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertTrue(first_device.closed)
        self.assertEqual(first_device.attach_calls, ["com.demo.target"])
        self.assertEqual(second_device.attach_calls, ["com.demo.target"])
        self.assertEqual(usb_calls[0]["remote_abstract"], "nook-gadget-com.demo.target")
        self.assertEqual(usb_calls[1].get("remote_abstract", ""), "")
        self.assertEqual(usb_calls[2]["remote_abstract"], "nook-gadget-com.demo.target")

    def test_repl_attach_bootstraps_loads_script_and_exits(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()
        stdin = io.StringIO("%exit\n")

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('repl-attach')")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "repl",
                    "attach",
                    "com.demo.target",
                    "-l",
                    script_path,
                ],
                device_factory=lambda **kwargs: device,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(device.attach_calls, ["com.demo.target"])
        self.assertEqual(device.resume_calls, [])
        self.assertEqual(device.call_order[:4], ["attach", "session.create_script", "script.create", "script.load"])
        self.assertTrue(device.closed)

    def test_repl_attach_ctrl_c_unloads_active_script(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        class InterruptingStdin:
            def readline(self):
                raise KeyboardInterrupt

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('repl-attach')")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "repl",
                    "attach",
                    "com.demo.target",
                    "-l",
                    script_path,
                ],
                device_factory=lambda **kwargs: device,
                stdin=InterruptingStdin(),
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(device.attach_calls, ["com.demo.target"])
        self.assertIn("script.unload", device.call_order)
        self.assertTrue(device.closed)

    def test_repl_spawn_can_start_suspended_and_exit(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()
        stdin = io.StringIO("%exit\n")

        exit_code = main(
            [
                "repl",
                "spawn",
                "com.demo.target",
            ],
            device_factory=lambda **kwargs: device,
            stdin=stdin,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(device.spawn_calls, [("com.demo.target", [], 10000)])
        self.assertEqual(device.resume_calls, [])
        self.assertEqual(len(device.spawn_session.created_scripts), 0)
        self.assertTrue(device.closed)

    def test_repl_spawn_can_load_script_while_still_suspended(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('repl-manual-load')")
            script_path = handle.name

        stdin = io.StringIO(f"%load {script_path}\n%resume\n%exit\n")

        try:
            exit_code = main(
                [
                    "repl",
                    "spawn",
                    "com.demo.target",
                ],
                device_factory=lambda **kwargs: device,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(len(device.spawn_session.created_scripts), 1)
        self.assertEqual(device.resume_calls, [4321])
        self.assertEqual(
            device.call_order[:5],
            ["spawn", "session.create_script", "script.create", "script.load", "resume"],
        )

    def test_repl_info_and_help_commands_print_state(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()
        stdin = io.StringIO("/info\n/help\n/exit\n")

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('repl-meta')")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "repl",
                    "spawn",
                    "com.demo.target",
                    "-l",
                    script_path,
                ],
                device_factory=lambda **kwargs: device,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        output = stdout.getvalue()
        self.assertIn("mode=spawn", output)
        self.assertIn("pid=4321", output)
        self.assertIn("process=com.demo.target", output)
        self.assertIn("script=1000", output)
        self.assertIn("resumed=no", output)
        self.assertIn("spawn_state=suspended", output)
        self.assertIn("/help", output)
        self.assertIn("/info", output)
        self.assertIn("/exit", output)

    def test_repl_info_reflects_resumed_spawn_state(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()
        stdin = io.StringIO("%resume\n%info\n%exit\n")

        exit_code = main(
            [
                "repl",
                "spawn",
                "com.demo.target",
            ],
            device_factory=lambda **kwargs: device,
            stdin=stdin,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        output = stdout.getvalue()
        self.assertIn("[*] Resuming pid 4321...", output)
        self.assertIn("[+] Process resumed", output)
        self.assertIn("resumed=yes", output)
        self.assertIn("spawn_state=resumed", output)

    def test_repl_spawn_with_script_loads_before_resume(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('repl-deferred')")
            script_path = handle.name

        stdin = io.StringIO("%resume\n%exit\n")

        try:
            exit_code = main(
                [
                    "repl",
                    "spawn",
                    "com.demo.target",
                    "-l",
                    script_path,
                ],
                device_factory=lambda **kwargs: device,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(device.resume_calls, [4321])
        self.assertEqual(len(device.spawn_session.created_scripts), 1)
        self.assertEqual(
            device.call_order[:5],
            ["spawn", "session.create_script", "script.create", "script.load", "resume"],
        )
        output = stdout.getvalue()
        self.assertIn("[*] Loading '%s'..." % os.path.basename(script_path), output)
        self.assertIn("[+] Script loaded (id: 1000)", output)
        self.assertIn("[*] Resuming pid 4321...", output)
        self.assertIn("[+] Process resumed", output)

    def test_repl_spawn_prints_banner_before_loading_and_resuming(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('banner-order')")
            script_path = handle.name

        stdin = io.StringIO("%exit\n")

        try:
            exit_code = main(
                [
                    "repl",
                    "spawn",
                    "com.demo.target",
                    "-l",
                    script_path,
                    "--resume",
                ],
                device_factory=lambda **kwargs: device,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        output = stdout.getvalue()
        banner_index = output.index("Dynamic instrumentation toolkit for Android")
        loading_index = output.index("[*] Loading")
        resuming_index = output.index("[*] Resuming")
        self.assertLess(banner_index, loading_index)
        self.assertLess(banner_index, resuming_index)
        self.assertEqual(output.count("Dynamic instrumentation toolkit for Android"), 1)

    def test_repl_unknown_command_prints_hint(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()
        stdin = io.StringIO("/wat\n/exit\n")

        exit_code = main(
            [
                "repl",
                "spawn",
                "com.demo.target",
            ],
            device_factory=lambda **kwargs: device,
            stdin=stdin,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertIn("[-] unknown command, type /help", stderr.getvalue())

    def test_repl_unload_reload_and_load_replace_active_script(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as first_handle:
            first_handle.write("console.log('first')")
            first_path = first_handle.name
        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as second_handle:
            second_handle.write("console.log('second')")
            second_path = second_handle.name

        stdin = io.StringIO(f"%unload\n%reload\n%load {second_path}\n%exit\n")

        try:
            exit_code = main(
                [
                    "repl",
                    "attach",
                    "com.demo.target",
                    "-l",
                    first_path,
                ],
                device_factory=lambda **kwargs: device,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(first_path)
            os.unlink(second_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(len(device.attach_session.created_scripts), 3)
        first_script, reloaded_script, second_script = device.attach_session.created_scripts
        self.assertTrue(first_script.unloaded)
        self.assertTrue(reloaded_script.unloaded)
        self.assertTrue(second_script.loaded)
        output = stdout.getvalue()
        self.assertIn("[+] Script unloaded (id: 1000)", output)
        self.assertIn("[*] Loading '%s'..." % os.path.basename(second_path), output)
        self.assertIn("[+] Script loaded (id: 1000)", output)

    def test_repl_unload_failure_keeps_active_script_and_session_alive(self) -> None:
        device = FakeDevice()
        device.attach_session.next_script_unload_error = RuntimeError("script is pinned")
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('pinned')")
            script_path = handle.name

        stdin = io.StringIO("%unload\n%call ping []\n%exit\n")

        try:
            exit_code = main(
                [
                    "repl",
                    "attach",
                    "com.demo.target",
                    "-l",
                    script_path,
                ],
                device_factory=lambda **kwargs: device,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertIn("[-] script unload failed: script is pinned", stderr.getvalue())
        script = device.attach_session.created_scripts[0]
        self.assertEqual(script.call_calls, [("ping", tuple(), None)])
        self.assertTrue(script.unloaded)
        output = stdout.getvalue()
        self.assertIn('[rpc] ping => {"value": "pong", "args": []}', output)
        self.assertIn("[+] Script unloaded (id: 1000)", output)

    def test_repl_exit_unloads_active_script(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('exit-cleanup')")
            script_path = handle.name

        stdin = io.StringIO("%exit\n")

        try:
            exit_code = main(
                [
                    "repl",
                    "attach",
                    "com.demo.target",
                    "-l",
                    script_path,
                ],
                device_factory=lambda **kwargs: device,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        script = device.attach_session.created_scripts[0]
        self.assertTrue(script.unloaded)
        self.assertIn("[+] Script unloaded (id: 1000)", stdout.getvalue())

    def test_repl_post_command_and_plain_text_use_active_script(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('post')")
            script_path = handle.name

        stdin = io.StringIO('%post {"type":"post","payload":"hello"}\nplain-text-post\n%exit\n')

        try:
            exit_code = main(
                [
                    "repl",
                    "attach",
                    "com.demo.target",
                    "-l",
                    script_path,
                ],
                device_factory=lambda **kwargs: device,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        script = device.attach_session.created_scripts[0]
        self.assertEqual(
            script.post_calls,
            [('{"type":"post","payload":"hello"}', b""), ("plain-text-post", b"")],
        )

    def test_repl_post_without_active_script_reports_error(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()
        stdin = io.StringIO('%post {"type":"post","payload":"hello"}\n%exit\n')

        exit_code = main(
            [
                "repl",
                "spawn",
                "com.demo.target",
            ],
            device_factory=lambda **kwargs: device,
            stdin=stdin,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertIn("no active script", stderr.getvalue())

    def test_repl_message_loop_prints_script_messages(self) -> None:
        device = FakeDevice()
        device.wait_for_script_message_results = [
            self._message(1000, '{"type":"send","payload":"hello-from-script"}'),
            KeyboardInterrupt(),
        ]
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('msg')")
            script_path = handle.name

        stdin = io.StringIO("%exit\n")

        try:
            exit_code = main(
                [
                    "repl",
                    "attach",
                    "com.demo.target",
                    "-l",
                    script_path,
                ],
                device_factory=lambda **kwargs: device,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertIn("[Local::com.demo.target] hello-from-script", stdout.getvalue())

    def test_repl_console_log_messages_use_device_process_prefix(self) -> None:
        device = FakeDevice()
        device.wait_for_script_message_results = [
            self._message(1000, '{"type":"log","level":"info","payload":"hello-log"}'),
            KeyboardInterrupt(),
        ]
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('msg-log')")
            script_path = handle.name

        stdin = io.StringIO("/exit\n")

        try:
            exit_code = main(
                [
                    "repl",
                    "attach",
                    "com.demo.target",
                    "-l",
                    script_path,
                ],
                device_factory=lambda **kwargs: device,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertIn("[Local::com.demo.target] hello-log", stdout.getvalue())

    def test_repl_message_loop_prints_multiline_script_messages(self) -> None:
        device = FakeDevice()
        device.wait_for_script_message_results = [
            self._message(1000, '{"type":"send","payload":"00000000  41 42 43\\n00000003  44 45 46"}'),
            KeyboardInterrupt(),
        ]
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('msg-multiline')")
            script_path = handle.name

        stdin = io.StringIO("%exit\n")

        try:
            exit_code = main(
                [
                    "repl",
                    "attach",
                    "com.demo.target",
                    "-l",
                    script_path,
                ],
                device_factory=lambda **kwargs: device,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        output = stdout.getvalue()
        self.assertIn("[Local::com.demo.target] 00000000  41 42 43", output)
        self.assertIn("00000003  44 45 46", output)

    def test_repl_call_and_resume_commands(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('call')")
            script_path = handle.name

        stdin = io.StringIO('%resume\n%call ping ["hello"]\n%call ping\n%resume\n%exit\n')

        try:
            exit_code = main(
                [
                    "repl",
                    "spawn",
                    "com.demo.target",
                    "-l",
                    script_path,
                ],
                device_factory=lambda **kwargs: device,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        script = device.spawn_session.created_scripts[0]
        self.assertEqual(script.call_calls, [("ping", ("hello",), None), ("ping", (), None)])
        self.assertEqual(device.resume_calls, [4321])
        output = stdout.getvalue()
        self.assertIn('[rpc] ping => {"value": "pong", "args": ["hello"]}', output)
        self.assertIn('[rpc] ping => {"value": "pong", "args": []}', output)
        self.assertIn("[*] Resuming pid 4321...", output)
        self.assertIn("[+] Process resumed", output)
        self.assertIn("[*] already resumed: pid=4321 state=resumed", output)
        self.assertIn("already resumed: pid=4321 state=resumed", output)

    def test_repl_slash_commands_alias_percent_commands(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('call')")
            script_path = handle.name

        stdin = io.StringIO('/resume\n/call ping ["hello"]\n/call ping\n/exit\n')

        try:
            exit_code = main(
                [
                    "repl",
                    "spawn",
                    "com.demo.target",
                    "-l",
                    script_path,
                ],
                device_factory=lambda **kwargs: device,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        script = device.spawn_session.created_scripts[0]
        self.assertEqual(script.call_calls, [("ping", ("hello",), None), ("ping", (), None)])
        self.assertEqual(device.resume_calls, [4321])
        output = stdout.getvalue()
        self.assertIn('[rpc] ping => {"value": "pong", "args": ["hello"]}', output)
        self.assertIn('[rpc] ping => {"value": "pong", "args": []}', output)
        self.assertIn("[+] Process resumed", output)

    def test_repl_resume_failure_reports_spawn_state_context(self) -> None:
        device = FakeDevice()
        device.next_resume_error = RuntimeError("spawned process is not ready to resume")
        stdout = io.StringIO()
        stderr = io.StringIO()
        stdin = io.StringIO("%resume\n%exit\n")

        exit_code = main(
            [
                "repl",
                "spawn",
                "com.demo.target",
            ],
            device_factory=lambda **kwargs: device,
            stdin=stdin,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertIn(
            "resume failed while spawn session is suspended: resume failed for 'com.demo.target': spawned process is not ready to resume",
            stderr.getvalue(),
        )
        self.assertEqual(device.resume_calls, [])

    def test_repl_call_bad_json_and_resume_attach_report_errors(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('call-error')")
            script_path = handle.name

        stdin = io.StringIO("%call ping bad-json\n%resume\n%exit\n")

        try:
            exit_code = main(
                [
                    "repl",
                    "attach",
                    "com.demo.target",
                    "-l",
                    script_path,
                ],
                device_factory=lambda **kwargs: device,
                stdin=stdin,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        error_output = stderr.getvalue()
        self.assertIn("invalid JSON array", error_output)
        self.assertIn("resume is only available for spawn mode", error_output)

    def test_spawn_command_loads_script_before_resuming(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('hello-cli')")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "spawn",
                    "com.demo.target",
                    "-l",
                    script_path,
                    "--oneshot",
                    "--resume",
                    "--agent-ready-timeout",
                    "9000",
                ],
                device_factory=lambda **kwargs: device,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(device.spawn_calls, [("com.demo.target", [], 9000)])
        self.assertEqual(device.resume_calls, [4321])
        self.assertEqual(len(device.spawn_session.created_scripts), 1)
        self.assertEqual(
            device.call_order[:5],
            ["spawn", "session.create_script", "script.create", "script.load", "resume"],
        )
        script = device.spawn_session.created_scripts[0]
        self.assertEqual(script.name, os.path.basename(script_path))
        self.assertEqual(script.source, "console.log('hello-cli')")
        self.assertTrue(script.created)
        self.assertTrue(script.loaded)
        self.assertIn("[*] Spawning 'com.demo.target'...", stdout.getvalue())
        self.assertIn("[*] Waiting for agent runtime ready...", stdout.getvalue())
        self.assertIn("[+] Spawned (pid: 4321)", stdout.getvalue())
        self.assertIn(f"[*] Loading '{os.path.basename(script_path)}'...", stdout.getvalue())
        self.assertIn("[+] Script loaded (id: 1000)", stdout.getvalue())
        self.assertIn("[*] Resuming pid 4321...", stdout.getvalue())
        self.assertIn("[+] Process resumed", stdout.getvalue())
        output = stdout.getvalue()
        self.assertLess(output.index("[*] Spawning 'com.demo.target'..."),
                        output.index("[*] Waiting for agent runtime ready..."))
        self.assertLess(output.index("[*] Waiting for agent runtime ready..."),
                        output.index(f"[*] Loading '{os.path.basename(script_path)}'..."))

    def test_spawn_command_defaults_to_interactive_repl(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()
        stdin = io.StringIO("%exit\n")

        exit_code = main(
            [
                "spawn",
                "com.demo.target",
            ],
            device_factory=lambda **kwargs: device,
            stdin=stdin,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(device.spawn_calls, [("com.demo.target", [], 10000)])
        self.assertIn("Dynamic instrumentation toolkit for Android", stdout.getvalue())
        self.assertIn("[Local::com.demo.target]", stdout.getvalue())

    def test_spawn_command_oneshot_keeps_non_interactive_behavior_and_prints_banner(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        exit_code = main(
            [
                "spawn",
                "com.demo.target",
                "--oneshot",
            ],
            device_factory=lambda **kwargs: device,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(device.spawn_calls, [("com.demo.target", [], 10000)])
        self.assertIn("Dynamic instrumentation toolkit for Android", stdout.getvalue())

    def test_attach_command_defaults_to_interactive_repl(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()
        stdin = io.StringIO("%exit\n")

        exit_code = main(
            [
                "attach",
                "com.demo.target",
            ],
            device_factory=lambda **kwargs: device,
            stdin=stdin,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(device.attach_calls, ["com.demo.target"])
        self.assertIn("Dynamic instrumentation toolkit for Android", stdout.getvalue())
        self.assertIn("[Local::com.demo.target]", stdout.getvalue())

    def test_attach_command_oneshot_keeps_non_interactive_behavior_and_prints_banner(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        exit_code = main(
            [
                "attach",
                "com.demo.target",
                "--oneshot",
            ],
            device_factory=lambda **kwargs: device,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(device.attach_calls, ["com.demo.target"])
        self.assertIn("Dynamic instrumentation toolkit for Android", stdout.getvalue())

    def test_spawn_command_can_emit_json(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('hello-json')")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "spawn",
                    "com.demo.target",
                    "-l",
                    script_path,
                    "--resume",
                    "--json",
                ],
                device_factory=lambda **kwargs: device,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        payload = json.loads(stdout.getvalue())
        self.assertEqual(payload["ok"], True)
        self.assertEqual(payload["pid"], 4321)
        self.assertEqual(payload["process_name"], "com.demo.target")
        self.assertEqual(payload["resumed"], True)
        self.assertEqual(
            payload["script"],
            {"id": 1000, "name": os.path.basename(script_path), "loaded": True},
        )

    def test_spawn_command_waits_and_prints_messages_until_interrupted(self) -> None:
        device = FakeDevice()
        device.wait_for_script_message_results = [
            self._message(1000, '{"type":"send","payload":"first"}'),
            self._message(1000, '{"type":"log","level":"info","payload":"second"}', b"\x01"),
            KeyboardInterrupt(),
            TimeoutError(),
        ]
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('wait-mode')")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "spawn",
                    "com.demo.target",
                    "-l",
                    script_path,
                    "--resume",
                    "--wait",
                    "--message-timeout",
                    "1234",
                ],
                device_factory=lambda **kwargs: device,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(
            device.wait_for_script_message_calls,
            [(1234, 1000), (1234, 1000), (1234, 1000), (100, 1000), (100, 0)],
        )
        script = device.spawn_session.created_scripts[0]
        self.assertTrue(script.unloaded)
        output = stdout.getvalue()
        self.assertIn("[Local::com.demo.target] first", output)
        self.assertIn("second", output)
        self.assertIn("[+] Script unloaded (id: 1000)", output)

    def test_console_log_warn_and_error_use_distinct_display_channels(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()
        console = Console(stdout=stdout, stderr=stderr, color=False)

        console.print_script_message_event(
            self._message(1000, '{"type":"log","level":"info","payload":"hello-log"}')
        )
        console.print_script_message_event(
            self._message(1000, '{"type":"log","level":"warn","payload":"hello-warn"}')
        )
        console.print_script_message_event(
            self._message(1000, '{"type":"log","level":"error","payload":"hello-error"}')
        )

        self.assertIn("hello-log", stdout.getvalue())
        self.assertNotIn("[*] hello-log", stdout.getvalue())
        self.assertIn("[!] hello-warn", stderr.getvalue())
        self.assertIn("[-] hello-error", stderr.getvalue())

    def test_script_message_prints_on_fresh_line_after_prompt(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()
        console = Console(stdout=stdout, stderr=stderr, color=False)
        console.bind_script_origin("USB Device", "com.demo.target")

        console.mark_prompt_active()
        stdout.write("[USB Device::com.demo.target]-> ")
        console.print_script_message_event(
            self._message(1000, '{"type":"send","payload":"hello-after-prompt"}')
        )

        self.assertEqual(
            stdout.getvalue(),
            "[USB Device::com.demo.target]-> \n[USB Device::com.demo.target] hello-after-prompt\n",
        )

    def test_script_message_prints_on_fresh_line_after_prompt_for_terminal_like_stream(self) -> None:
        stdout = CaptureStream()
        stderr = CaptureStream()
        console = Console(stdout=stdout, stderr=stderr, color=False)
        console.bind_script_origin("USB Device", "com.demo.target")

        console.mark_prompt_active()
        stdout.write("[USB Device::com.demo.target]-> ")
        console.print_script_message_event(
            self._message(1000, '{"type":"send","payload":"hello-after-prompt"}')
        )

        self.assertEqual(
            stdout.get_captured(),
            "[USB Device::com.demo.target]-> \n[USB Device::com.demo.target] hello-after-prompt\n",
        )

    def test_script_message_prints_on_fresh_line_when_it_arrives_during_prompt_flush(self) -> None:
        placeholder = CaptureStream()
        console = Console(stdout=placeholder, stderr=CaptureStream(), color=False)
        console.bind_script_origin("USB Device", "com.demo.target")
        stdout = PromptRaceStream(
            console,
            lambda: console.print_script_message_event(
                self._message(1000, '{"type":"send","payload":"hello-during-flush"}')
            ),
        )
        console.stdout = stdout

        console.write_prompt("[USB Device::com.demo.target]-> ")

        self.assertEqual(
            stdout.get_captured(),
            "[USB Device::com.demo.target]-> \n[USB Device::com.demo.target] hello-during-flush\n",
        )

    def test_attach_command_waits_and_emits_json_lines(self) -> None:
        device = FakeDevice()
        device.wait_for_script_message_results = [
            self._message(1000, '{"type":"send","payload":"attached"}'),
            KeyboardInterrupt(),
            TimeoutError(),
        ]
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('attach-wait')")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "attach",
                    "com.demo.target",
                    "-l",
                    script_path,
                    "--wait",
                    "--message-timeout",
                    "2222",
                    "--json",
                ],
                device_factory=lambda **kwargs: device,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(
            device.wait_for_script_message_calls,
            [(2222, 1000), (2222, 1000), (100, 1000), (100, 0)],
        )
        lines = [json.loads(line) for line in stdout.getvalue().splitlines()]
        self.assertEqual(lines[0]["ok"], True)
        self.assertEqual(lines[0]["session_id"], 7)
        self.assertEqual(lines[1], {
            "ok": True,
            "event": "script_message",
            "script_id": 1000,
            "message": '{"type":"send","payload":"attached"}',
            "data_len": 0,
        })
        script = device.attach_session.created_scripts[0]
        self.assertTrue(script.unloaded)

    def test_attach_command_waits_and_unloads_script_on_interrupt(self) -> None:
        device = FakeDevice()
        device.wait_for_script_message_results = [KeyboardInterrupt(), TimeoutError()]
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('attach-wait-unload')")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "attach",
                    "com.demo.target",
                    "-l",
                    script_path,
                    "--wait",
                    "--message-timeout",
                    "2222",
                ],
                device_factory=lambda **kwargs: device,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        script = device.attach_session.created_scripts[0]
        self.assertTrue(script.unloaded)
        self.assertIn("[+] Script unloaded (id: 1000)", stdout.getvalue())

    def test_attach_command_wait_cleanup_drains_unload_messages(self) -> None:
        device = FakeDevice()
        device.wait_for_script_message_results = [
            KeyboardInterrupt(),
            self._message(1000, '{"type":"send","payload":"unload-fired"}'),
            TimeoutError(),
        ]
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('attach-wait-unload-drain')")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "attach",
                    "com.demo.target",
                    "-l",
                    script_path,
                    "--wait",
                    "--message-timeout",
                    "2222",
                ],
                device_factory=lambda **kwargs: device,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(
            device.wait_for_script_message_calls,
            [(2222, 1000), (100, 1000), (100, 1000), (100, 0)],
        )
        self.assertIn(
            "[Local::com.demo.target] unload-fired",
            stdout.getvalue(),
        )
        self.assertIn("[+] Script unloaded (id: 1000)", stdout.getvalue())

    def test_spawn_command_interactive_posts_stdin_lines(self) -> None:
        device = FakeDevice()
        device.wait_for_script_message_results = [KeyboardInterrupt()]
        stdout = io.StringIO()
        stderr = io.StringIO()
        stdin = io.StringIO('{"type":"post","payload":"hello-from-stdin"}\n')

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('interactive')")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "spawn",
                    "com.demo.target",
                    "-l",
                    script_path,
                    "--resume",
                    "--wait",
                    "--interactive",
                ],
                device_factory=lambda **kwargs: device,
                stdout=stdout,
                stderr=stderr,
                stdin=stdin,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        script = device.spawn_session.created_scripts[0]
        self.assertTrue(script.post_event.wait(1.0))
        self.assertEqual(
            script.post_calls,
            [('{"type":"post","payload":"hello-from-stdin"}', b"")],
        )

    def test_spawn_command_can_call_rpc(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("rpc.exports = { ping(name) { return { value: 'pong', name: name }; } }")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "spawn",
                    "com.demo.target",
                    "-l",
                    script_path,
                    "--call",
                    "ping",
                    "--call-args",
                    '["hello"]',
                ],
                device_factory=lambda **kwargs: device,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        script = device.spawn_session.created_scripts[0]
        self.assertEqual(script.call_calls, [("ping", ("hello",), None)])
        self.assertIn('[rpc] ping => {"value": "pong", "args": ["hello"]}', stdout.getvalue())

    def test_call_command_can_spawn_load_before_resume_and_call_rpc(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("rpc.exports = { ping(name) { return { value: 'pong', name: name }; } }")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "call",
                    "com.demo.target",
                    "-l",
                    script_path,
                    "ping",
                    "--call-args",
                    '["hello"]',
                    "--spawn",
                    "--resume",
                ],
                device_factory=lambda **kwargs: device,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(device.spawn_calls, [("com.demo.target", [], 10000)])
        self.assertEqual(device.resume_calls, [4321])
        self.assertEqual(
            device.call_order[:6],
            ["spawn", "session.create_script", "script.create", "script.load", "resume", "script.call"],
        )
        script = device.spawn_session.created_scripts[0]
        self.assertEqual(script.call_calls, [("ping", ("hello",), None)])
        output = stdout.getvalue()
        self.assertIn("[*] Spawning 'com.demo.target'...", output)
        self.assertIn("[*] Waiting for agent runtime ready...", output)
        self.assertIn("[+] Spawned (pid: 4321)", output)
        self.assertIn("[*] Loading '%s'..." % os.path.basename(script_path), output)
        self.assertIn("[+] Script loaded (id: 1000)", output)
        self.assertIn("[*] Resuming pid 4321...", output)
        self.assertIn("[+] Process resumed", output)
        self.assertIn('[rpc] ping => {"value": "pong", "args": ["hello"]}', output)

    def test_call_command_can_attach_load_and_call_rpc(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("rpc.exports = { ping(name) { return { value: 'pong', name: name }; } }")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "call",
                    "com.demo.target",
                    "-l",
                    script_path,
                    "ping",
                    "--call-args",
                    '["hello"]',
                    "--attach",
                    "--json",
                ],
                device_factory=lambda **kwargs: device,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(device.attach_calls, ["com.demo.target"])
        script = device.attach_session.created_scripts[0]
        self.assertEqual(script.call_calls, [("ping", ("hello",), None)])
        payload = json.loads(stdout.getvalue())
        self.assertEqual(payload["ok"], True)
        self.assertEqual(payload["mode"], "attach")
        self.assertEqual(payload["session_id"], 7)
        self.assertEqual(payload["pid"], 2100)
        self.assertEqual(payload["rpc"], {"method": "ping", "result": {"value": "pong", "args": ["hello"]}})

    def test_call_command_requires_exactly_one_mode(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("rpc.exports = {}")
            script_path = handle.name

        try:
            exit_code_missing = main(
                [
                    "call",
                    "com.demo.target",
                    "-l",
                    script_path,
                    "ping",
                ],
                device_factory=lambda **kwargs: device,
                stdout=stdout,
                stderr=stderr,
            )
            exit_code_both = main(
                [
                    "call",
                    "com.demo.target",
                    "-l",
                    script_path,
                    "ping",
                    "--spawn",
                    "--attach",
                ],
                device_factory=lambda **kwargs: device,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code_missing, 1)
        self.assertEqual(exit_code_both, 1)
        error_output = stderr.getvalue()
        self.assertIn("[-] call command requires exactly one of --spawn or --attach", error_output)

    def test_attach_command_interactive_json_still_streams_messages(self) -> None:
        device = FakeDevice()
        device.wait_for_script_message_results = [
            self._message(1000, '{"type":"send","payload":"attached"}'),
            KeyboardInterrupt(),
        ]
        stdout = io.StringIO()
        stderr = io.StringIO()
        stdin = io.StringIO('{"type":"post","payload":"ping"}\n')

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('attach-interactive')")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "attach",
                    "com.demo.target",
                    "-l",
                    script_path,
                    "--wait",
                    "--interactive",
                    "--json",
                ],
                device_factory=lambda **kwargs: device,
                stdout=stdout,
                stderr=stderr,
                stdin=stdin,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        script = device.attach_session.created_scripts[0]
        self.assertTrue(script.post_event.wait(1.0))
        self.assertEqual(script.post_calls, [('{"type":"post","payload":"ping"}', b"")])
        lines = [json.loads(line) for line in stdout.getvalue().splitlines()]
        self.assertEqual(lines[1]["event"], "script_message")

    def test_attach_command_supports_identifier(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        exit_code = main(
            ["attach", "com.demo.target", "--oneshot"],
            device_factory=lambda **kwargs: device,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(device.attach_calls, ["com.demo.target"])
        self.assertIn("[*] Attaching to 'com.demo.target'...", stdout.getvalue())
        self.assertIn("[*] Waiting for agent runtime ready...", stdout.getvalue())
        self.assertIn("[+] Attached (pid: 2100, session: 7)", stdout.getvalue())

    def test_attach_command_can_load_script(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('attach-load')")
            script_path = handle.name

        try:
            exit_code = main(
                ["attach", "com.demo.target", "-l", script_path, "--oneshot"],
                device_factory=lambda **kwargs: device,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(device.attach_calls, ["com.demo.target"])
        self.assertEqual(len(device.attach_session.created_scripts), 1)
        script = device.attach_session.created_scripts[0]
        self.assertEqual(script.name, os.path.basename(script_path))
        self.assertEqual(script.source, "console.log('attach-load')")
        self.assertTrue(script.created)
        self.assertTrue(script.loaded)
        output = stdout.getvalue()
        self.assertIn("[*] Attaching to 'com.demo.target'...", output)
        self.assertIn("[*] Waiting for agent runtime ready...", output)
        self.assertIn("[+] Attached (pid: 2100, session: 7)", output)
        self.assertIn("[*] Loading '%s'..." % os.path.basename(script_path), output)
        self.assertIn("[+] Script loaded (id: 1000)", output)

    def test_attach_command_can_load_script_and_resume_gadget_wait_session(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('attach-gadget-wait')")
            script_path = handle.name

        try:
            exit_code = main(
                ["attach", "com.demo.target", "--usb", "--gadget", "-l", script_path, "--oneshot"],
                device_factory=lambda **kwargs: None,
                usb_device_factory=lambda **kwargs: device,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(device.attach_calls, ["com.demo.target"])
        self.assertEqual(device.resume_calls, [2100])
        self.assertEqual(
            device.call_order[:5],
            ["attach", "session.create_script", "script.create", "script.load", "resume"],
        )
        output = stdout.getvalue()
        self.assertIn("[*] Resuming pid 2100...", output)
        self.assertIn("[+] Process resumed", output)

    def test_spawn_command_reports_script_load_failure(self) -> None:
        device = FakeDevice()
        device.spawn_session.next_script_load_error = RuntimeError("script load callback failed")
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('broken-load')")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "spawn",
                    "com.demo.target",
                    "-l",
                    script_path,
                    "--resume",
                ],
                device_factory=lambda **kwargs: device,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 1)
        self.assertIn("script load callback failed", stderr.getvalue())
        self.assertNotIn("[+] Process resumed", stdout.getvalue())

    def test_spawn_command_reports_agent_ready_timeout_with_stage_context(self) -> None:
        class SpawnTimeoutDevice(FakeDevice):
            def spawn(self, identifier: str, argv=None, agent_ready_timeout_ms=None):
                raise TimeoutError("wait runtime agent ready timed out")

        stdout = io.StringIO()
        stderr = io.StringIO()

        exit_code = main(
            [
                "spawn",
                "com.demo.target",
                "--resume",
            ],
            device_factory=lambda **kwargs: SpawnTimeoutDevice(),
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 1)
        self.assertIn(
            "[-] spawn agent-ready timed out for 'com.demo.target': wait runtime agent ready timed out",
            stderr.getvalue(),
        )

    def test_spawn_command_reports_spawn_response_timeout_with_stage_context(self) -> None:
        class SpawnTimeoutDevice(FakeDevice):
            def spawn(self, identifier: str, argv=None, agent_ready_timeout_ms=None):
                raise TimeoutError("wait spawn response timed out")

        stdout = io.StringIO()
        stderr = io.StringIO()

        exit_code = main(
            [
                "spawn",
                "com.demo.target",
                "--resume",
            ],
            device_factory=lambda **kwargs: SpawnTimeoutDevice(),
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 1)
        self.assertIn(
            "[-] spawn agent-ready timed out for 'com.demo.target': wait spawn response timed out",
            stderr.getvalue(),
        )

    def test_attach_command_reports_load_timeout_with_stage_context(self) -> None:
        device = FakeDevice()
        device.attach_session.next_script_load_error = TimeoutError("operation timed out")
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.log('attach-timeout')")
            script_path = handle.name

        try:
            exit_code = main(
                ["attach", "com.demo.target", "-l", script_path, "--oneshot"],
                device_factory=lambda **kwargs: device,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 1)
        self.assertIn(
            "[-] script load timed out for '%s': operation timed out" % os.path.basename(script_path),
            stderr.getvalue(),
        )

    def test_detach_and_resume_commands(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        detach_exit = main(
            ["detach", "7"],
            device_factory=lambda **kwargs: device,
            stdout=stdout,
            stderr=stderr,
        )
        resume_exit = main(
            ["resume", "2100"],
            device_factory=lambda **kwargs: device,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(detach_exit, 0)
        self.assertEqual(resume_exit, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(device.detach_calls, [7])
        self.assertEqual(device.resume_calls, [2100])
        output = stdout.getvalue()
        self.assertIn("[*] Detaching...", output)
        self.assertIn("[+] Detached", output)
        self.assertIn("[*] Resuming pid 2100...", output)
        self.assertIn("[+] Process resumed", output)

    def test_post_command_spawns_loads_script_and_posts_message(self) -> None:
        device = FakeDevice()
        device.wait_for_script_message_default = self._message(
            0, '{"type":"send","payload":"script-post-received"}'
        )
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("recv(function (message) { send({ type: 'send', payload: message.payload }); });")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "post",
                    "com.demo.target",
                    '{"type":"post","payload":"hello-from-host"}',
                    "-l",
                    script_path,
                    "--agent-ready-timeout",
                    "8000",
                    "--message-timeout",
                    "3000",
                ],
                device_factory=lambda **kwargs: device,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(device.spawn_calls, [("com.demo.target", [], 8000)])
        self.assertEqual(device.resume_calls, [4321])
        self.assertEqual(len(device.spawn_session.created_scripts), 1)
        self.assertEqual(
            device.call_order[:6],
            ["spawn", "session.create_script", "script.create", "script.load", "resume", "script.post"],
        )
        script = device.spawn_session.created_scripts[0]
        self.assertTrue(script.created)
        self.assertTrue(script.loaded)
        self.assertEqual(
            script.post_calls,
            [('{"type":"post","payload":"hello-from-host"}', b"")],
        )
        self.assertEqual(device.wait_for_script_message_calls, [(3000, 1000)])
        output = stdout.getvalue()
        self.assertIn("[*] Spawning 'com.demo.target'...", output)
        self.assertIn("[*] Waiting for agent runtime ready...", output)
        self.assertIn("[+] Spawned (pid: 4321)", output)
        self.assertIn("[*] Loading '%s'..." % os.path.basename(script_path), output)
        self.assertIn("[+] Script loaded (id: 1000)", output)
        self.assertIn("[*] Resuming pid 4321...", output)
        self.assertIn("[+] Process resumed", output)
        self.assertIn("[*] Posted message to script", output)
        self.assertIn("[Local::com.demo.target] script-post-received", output)

    def test_post_command_requires_script(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        exit_code = main(
            [
                "post",
                "com.demo.target",
                '{"type":"post","payload":"hello-from-host"}',
            ],
            device_factory=lambda **kwargs: device,
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 1)
        self.assertIn("[-] post command requires -l/--load", stderr.getvalue())

    def test_unload_command_creates_loads_and_unloads_script(self) -> None:
        device = FakeDevice()
        stdout = io.StringIO()
        stderr = io.StringIO()

        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as handle:
            handle.write("console.warn('before-unload')")
            script_path = handle.name

        try:
            exit_code = main(
                [
                    "unload",
                    "com.demo.target",
                    script_path,
                    "--agent-ready-timeout",
                    "7000",
                ],
                device_factory=lambda **kwargs: device,
                stdout=stdout,
                stderr=stderr,
            )
        finally:
            os.unlink(script_path)

        self.assertEqual(exit_code, 0)
        self.assertEqual(stderr.getvalue(), "")
        self.assertEqual(device.spawn_calls, [("com.demo.target", [], 7000)])
        self.assertEqual(device.resume_calls, [4321])
        self.assertEqual(len(device.spawn_session.created_scripts), 1)
        self.assertEqual(
            device.call_order[:6],
            ["spawn", "session.create_script", "script.create", "script.load", "resume", "script.unload"],
        )
        script = device.spawn_session.created_scripts[0]
        self.assertTrue(script.created)
        self.assertTrue(script.loaded)
        self.assertTrue(script.unloaded)
        output = stdout.getvalue()
        self.assertIn("[*] Spawning 'com.demo.target'...", output)
        self.assertIn("[+] Spawned (pid: 4321)", output)
        self.assertIn("[*] Loading '%s'..." % os.path.basename(script_path), output)
        self.assertIn("[+] Script loaded (id: 1000)", output)
        self.assertIn("[*] Resuming pid 4321...", output)
        self.assertIn("[+] Process resumed", output)
        self.assertIn("[+] Script unloaded (id: 1000)", output)

    def test_non_json_error_output_uses_frida_style_prefix(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()

        exit_code = main(
            ["post", "com.demo.target", '{"type":"post","payload":"hello"}'],
            device_factory=lambda **kwargs: FakeDevice(),
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 1)
        self.assertIn("Dynamic instrumentation toolkit for Android", stdout.getvalue())
        self.assertIn("[-] post command requires -l/--load", stderr.getvalue())

    def test_json_error_output_is_structured(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()

        exit_code = main(
            ["apps", "--json"],
            device_factory=lambda **kwargs: ErrorDevice("boom"),
            stdout=stdout,
            stderr=stderr,
        )

        self.assertEqual(exit_code, 1)
        self.assertEqual(stdout.getvalue(), "")
        payload = json.loads(stderr.getvalue())
        self.assertEqual(payload, {"ok": False, "error": "boom"})


if __name__ == "__main__":
    unittest.main()
