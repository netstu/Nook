import io
import json
import os
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock


TEST_ROOT = os.path.dirname(__file__)
PACKAGE_ROOT = os.path.abspath(os.path.join(TEST_ROOT, ".."))
if PACKAGE_ROOT not in sys.path:
    sys.path.insert(0, PACKAGE_ROOT)


from nook import sodump  # noqa: E402
from nook.sofix.rebuilder import RepairResult  # noqa: E402


class FakeMessage:
    def __init__(self, script_id: int, message: str, data: bytes = b"") -> None:
        self.script_id = script_id
        self.message = message
        self.data = data


class FakeSoDumpScript:
    def __init__(self, modules, dump_messages, call_order) -> None:
        self.script_id = 654
        self.name = "sodump.js"
        self._modules = list(modules)
        self._dump_messages = list(dump_messages)
        self._call_order = call_order
        self.call_calls = []
        self.unloaded = False

    def create(self) -> int:
        self._call_order.append("script.create")
        return self.script_id

    def load(self):
        self._call_order.append("script.load")
        return None

    def unload(self):
        self._call_order.append("script.unload")
        self.unloaded = True
        return None

    def call(self, method: str, *args, timeout_ms=None):
        self._call_order.append(f"script.call:{method}")
        self.call_calls.append((method, args, timeout_ms))
        if method == "listmodules":
            return self._modules
        if method == "findmodule":
            module_name = args[0]
            for module in self._modules:
                if module["name"] == module_name:
                    return module
            return None
        if method == "beginmoduledump":
            return {
                "token": "sodump-0001",
                "size": 8,
                "chunk_size": 4,
                "chunks": 2,
            }
        raise AssertionError(f"unexpected rpc method: {method}")

    def wait_for_message(self, timeout_ms=None):
        self._call_order.append("script.wait_for_message")
        if not self._dump_messages:
            raise TimeoutError("operation timed out")
        return self._dump_messages.pop(0)


class FakeSoDumpSession:
    def __init__(self, pid: int, process_name: str, script: FakeSoDumpScript, call_order) -> None:
        self.pid = pid
        self.process_name = process_name
        self.session_id = 55
        self._script = script
        self._call_order = call_order

    def create_script(self, source: str, name: str = "script.js"):
        self._call_order.append("session.create_script")
        return self._script


class FakeSoDumpDevice:
    def __init__(self, session: FakeSoDumpSession, call_order) -> None:
        self._session = session
        self._call_order = call_order
        self.attach_calls = []
        self.spawn_calls = []
        self.resume_calls = []

    def attach(self, target):
        self._call_order.append("attach")
        self.attach_calls.append(target)
        return self._session

    def spawn(self, identifier: str, argv=None, agent_ready_timeout_ms=None):
        self._call_order.append("spawn")
        self.spawn_calls.append((identifier, list(argv or []), agent_ready_timeout_ms))
        return self._session

    def resume(self, pid: int):
        self._call_order.append("resume")
        self.resume_calls.append(pid)


class SoDumpTests(unittest.TestCase):
    def test_default_output_dir_uses_target_name(self) -> None:
        result = sodump.default_output_dir("com.demo.target")
        self.assertTrue(result.endswith("com.demo.target-sodump"))

    def test_collects_binary_chunks_for_matching_token(self) -> None:
        script = FakeSoDumpScript(
            modules=[],
            dump_messages=[
                FakeMessage(
                    654,
                    json.dumps(
                        {
                            "type": "sodump-chunk",
                            "token": "sodump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"ABCD",
                ),
                FakeMessage(
                    654,
                    json.dumps(
                        {
                            "type": "sodump-chunk",
                            "token": "sodump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"EFGH",
                ),
            ],
            call_order=[],
        )

        result = sodump.collect_dump_bytes(
            script,
            token="sodump-0001",
            expected_chunks=2,
            timeout_ms=1000,
        )

        self.assertEqual(result, b"ABCDEFGH")

    def test_attach_mode_lists_modules_when_module_is_omitted(self) -> None:
        call_order = []
        script = FakeSoDumpScript(
            modules=[
                {"name": "liba.so", "base": "0x1000", "size": 16, "path": "/data/app/liba.so"},
                {"name": "libb.so", "base": "0x2000", "size": 32, "path": "/data/app/libb.so"},
            ],
            dump_messages=[],
            call_order=call_order,
        )
        session = FakeSoDumpSession(777, "com.demo.target", script, call_order)
        device = FakeSoDumpDevice(session, call_order)
        stdout = io.StringIO()
        stderr = io.StringIO()

        options = SimpleNamespace(
            spawn_package=None,
            target="com.demo.target",
            module=None,
            output_dir=None,
            sleep_ms=0,
            fix=True,
            agent_ready_timeout=10000,
            message_timeout=1000,
            debug=False,
            json=False,
        )

        result = sodump.run_sodump(options, device, stdout=stdout, stderr=stderr)

        self.assertTrue(result["ok"])
        self.assertTrue(result["listed"])
        self.assertEqual(result["module_count"], 2)
        self.assertIn("liba.so", stdout.getvalue())
        self.assertIn("libb.so", stdout.getvalue())

    def test_attach_mode_writes_raw_fix_and_metadata(self) -> None:
        call_order = []
        script = FakeSoDumpScript(
            modules=[
                {"name": "libdemo.so", "base": "0x1000", "size": 8, "path": "/data/app/libdemo.so"},
            ],
            dump_messages=[
                FakeMessage(
                    654,
                    json.dumps(
                        {
                            "type": "sodump-chunk",
                            "token": "sodump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"\x7fELF",
                ),
                FakeMessage(
                    654,
                    json.dumps(
                        {
                            "type": "sodump-chunk",
                            "token": "sodump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"DATA",
                ),
            ],
            call_order=call_order,
        )
        session = FakeSoDumpSession(1337, "com.demo.target", script, call_order)
        device = FakeSoDumpDevice(session, call_order)

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                module="libdemo.so",
                output_dir=temp_dir,
                base_so="E:\\refs\\libdemo.so",
                sleep_ms=0,
                fix=True,
                agent_ready_timeout=10000,
                message_timeout=1000,
                debug=False,
                json=False,
            )

            with mock.patch(
                "nook.sodump.rebuild_loaded_elf_image",
                return_value=RepairResult(
                    data=b"\x7fELFFIX",
                    modified_program_headers=1,
                    warnings=["dynamic layout inferred"],
                    synthesized_sections=3,
                ),
            ) as rebuild:
                result = sodump.run_sodump(options, device, stdout=io.StringIO(), stderr=io.StringIO())

            raw_path = os.path.join(temp_dir, "libdemo.so.raw.so")
            fix_path = os.path.join(temp_dir, "libdemo.so.fix.so")
            metadata_path = os.path.join(temp_dir, "libdemo.so.json")
            self.assertTrue(os.path.exists(raw_path))
            self.assertTrue(os.path.exists(fix_path))
            self.assertTrue(os.path.exists(metadata_path))
            with open(metadata_path, "r", encoding="utf-8") as handle:
                metadata = json.load(handle)
            self.assertTrue(result["ok"])
            self.assertTrue(metadata["artifacts"][0]["fix_applied"])
            self.assertTrue(metadata["artifacts"][0]["fix_success"])
            self.assertEqual(metadata["artifacts"][0]["repair_warnings"], ["dynamic layout inferred"])
            self.assertEqual(metadata["artifacts"][0]["synthesized_sections"], 3)
            self.assertEqual(rebuild.call_args.kwargs["base_so_path"], "E:\\refs\\libdemo.so")

    def test_repair_failure_preserves_raw_and_metadata(self) -> None:
        call_order = []
        script = FakeSoDumpScript(
            modules=[
                {"name": "libdemo.so", "base": "0x1000", "size": 8, "path": "/data/app/libdemo.so"},
            ],
            dump_messages=[
                FakeMessage(
                    654,
                    json.dumps(
                        {
                            "type": "sodump-chunk",
                            "token": "sodump-0001",
                            "index": 0,
                            "chunks": 2,
                            "size": 4,
                            "eof": False,
                        }
                    ),
                    b"\x7fELF",
                ),
                FakeMessage(
                    654,
                    json.dumps(
                        {
                            "type": "sodump-chunk",
                            "token": "sodump-0001",
                            "index": 1,
                            "chunks": 2,
                            "size": 4,
                            "eof": True,
                        }
                    ),
                    b"FAIL",
                ),
            ],
            call_order=call_order,
        )
        session = FakeSoDumpSession(1337, "com.demo.target", script, call_order)
        device = FakeSoDumpDevice(session, call_order)

        with tempfile.TemporaryDirectory() as temp_dir:
            options = SimpleNamespace(
                spawn_package=None,
                target="com.demo.target",
                module="libdemo.so",
                output_dir=temp_dir,
                base_so=None,
                sleep_ms=0,
                fix=True,
                agent_ready_timeout=10000,
                message_timeout=1000,
                debug=False,
                json=False,
            )

            with mock.patch("nook.sodump.rebuild_loaded_elf_image", side_effect=ValueError("repair failed")):
                result = sodump.run_sodump(options, device, stdout=io.StringIO(), stderr=io.StringIO())

            raw_path = os.path.join(temp_dir, "libdemo.so.raw.so")
            metadata_path = os.path.join(temp_dir, "libdemo.so.json")
            self.assertTrue(os.path.exists(raw_path))
            self.assertTrue(os.path.exists(metadata_path))
            self.assertFalse(result["ok"])
            with open(metadata_path, "r", encoding="utf-8") as handle:
                metadata = json.load(handle)
            self.assertEqual(metadata["artifacts"][0]["repair_error"], "repair failed")


if __name__ == "__main__":
    unittest.main()
