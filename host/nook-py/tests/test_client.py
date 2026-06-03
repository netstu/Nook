import os
import socket
import sys
import threading
import time
import unittest


TEST_ROOT = os.path.dirname(__file__)
PACKAGE_ROOT = os.path.abspath(os.path.join(TEST_ROOT, ".."))
if PACKAGE_ROOT not in sys.path:
    sys.path.insert(0, PACKAGE_ROOT)


from nook.device import Device  # noqa: E402
from nook.protocol import (  # noqa: E402
    AgentReady,
    AgentReadyStage,
    AppEntry,
    AppListResponse,
    AttachResponse,
    ErrorInfo,
    Frame,
    MessageType,
    ProcessEntry,
    ProcessListResponse,
    ResumeResponse,
    ScriptCreateResponse,
    ScriptMessage,
    RpcResponse,
    ScriptResponse,
    SpawnResponse,
    decode_resume_request,
    decode_script_create,
    decode_script_load,
    decode_script_post,
    decode_script_unload,
    decode_spawn_request,
    encode_agent_ready,
    encode_app_list_response,
    encode_attach_response,
    encode_process_list_response,
    encode_rpc_response,
    encode_resume_response,
    encode_script_create_response,
    encode_script_message,
    encode_script_response,
    encode_spawn_response,
)


class FakeConnection:
    def __init__(self) -> None:
        self.sent_frames = []
        self._incoming = []
        self._cv = threading.Condition()

    def send_frame(self, frame: Frame) -> None:
        with self._cv:
            self.sent_frames.append(frame)
            self._cv.notify_all()

    def recv_frame(self, timeout_ms: int = None) -> Frame:
        timeout = None if timeout_ms is None else timeout_ms / 1000.0
        with self._cv:
            if not self._incoming:
                ok = self._cv.wait_for(lambda: bool(self._incoming), timeout=timeout)
                if not ok:
                    raise TimeoutError("recv frame timed out")
            return self._incoming.pop(0)

    def push_incoming(self, frame: Frame) -> None:
        with self._cv:
            self._incoming.append(frame)
            self._cv.notify_all()

    def wait_for_sent_frame(self, message_type: MessageType, timeout_ms: int = 1000) -> Frame:
        timeout = timeout_ms / 1000.0
        with self._cv:
            ok = self._cv.wait_for(
                lambda: any(frame.message_type == message_type for frame in self.sent_frames),
                timeout=timeout,
            )
            if not ok:
                raise TimeoutError(f"frame not sent: {message_type}")
            for index, frame in enumerate(self.sent_frames):
                if frame.message_type == message_type:
                    return self.sent_frames.pop(index)
        raise AssertionError("unreachable")


class TimeoutThenFrameConnection(FakeConnection):
    def __init__(self) -> None:
        super().__init__()
        self._inject_timeout = True

    def recv_frame(self, timeout_ms: int = None) -> Frame:
        if self._inject_timeout:
            self._inject_timeout = False
            raise socket.timeout("timed out")
        return super().recv_frame(timeout_ms=timeout_ms)


class DeviceTests(unittest.TestCase):
    def test_attach_waits_for_runtime_agent_ready(self) -> None:
        connection = FakeConnection()
        device = Device(connection, default_timeout_ms=1000)

        def attach_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.ATTACH_REQUEST)
            connection.push_incoming(
                Frame(
                    MessageType.ATTACH_RESPONSE,
                    request_frame.message_id,
                    encode_attach_response(
                        AttachResponse(
                            session_id=7,
                            pid=2100,
                            process_name="com.demo.target",
                            error=ErrorInfo(),
                        )
                    ),
                )
            )
            connection.push_incoming(
                Frame(
                    MessageType.AGENT_READY,
                    98,
                    encode_agent_ready(
                        AgentReady(
                            pid=2100,
                            process_name="com.demo.target",
                            spawn_token="attach-token-control",
                            arch="arm64",
                            version="0.1.0",
                            stage=AgentReadyStage.CONTROL,
                        )
                    ),
                )
            )
            connection.push_incoming(
                Frame(
                    MessageType.AGENT_READY,
                    99,
                    encode_agent_ready(
                        AgentReady(
                            pid=2100,
                            process_name="com.demo.target",
                            spawn_token="attach-token-runtime",
                            arch="arm64",
                            version="0.1.0",
                            stage=AgentReadyStage.RUNTIME,
                        )
                    ),
                )
            )

        threading.Thread(target=attach_responder).start()
        session = device.attach("com.demo.target")
        self.assertEqual(session.session_id, 7)
        self.assertEqual(session.pid, 2100)
        self.assertEqual(session.process_name, "com.demo.target")

        device.close()

    def test_attach_reports_runtime_ready_timeout(self) -> None:
        connection = FakeConnection()
        device = Device(connection, default_timeout_ms=1000)

        def attach_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.ATTACH_REQUEST)
            connection.push_incoming(
                Frame(
                    MessageType.ATTACH_RESPONSE,
                    request_frame.message_id,
                    encode_attach_response(
                        AttachResponse(
                            session_id=7,
                            pid=2100,
                            process_name="com.demo.target",
                            error=ErrorInfo(),
                        )
                    ),
                )
            )

        threading.Thread(target=attach_responder).start()
        with self.assertRaisesRegex(TimeoutError, "wait runtime agent ready timed out"):
            device.attach("com.demo.target", timeout_ms=20)

        device.close()

    def test_spawn_reports_spawn_response_timeout(self) -> None:
        connection = FakeConnection()
        device = Device(connection, default_timeout_ms=20)

        with self.assertRaisesRegex(TimeoutError, "wait spawn response timed out"):
            device.spawn("com.demo.target")

        device.close()

    def test_spawn_reports_agent_ready_timeout(self) -> None:
        connection = FakeConnection()
        device = Device(connection, default_timeout_ms=1000)

        def spawn_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SPAWN_REQUEST)
            connection.push_incoming(
                Frame(
                    MessageType.SPAWN_RESPONSE,
                    request_frame.message_id,
                    encode_spawn_response(SpawnResponse(pid=4321, error=ErrorInfo())),
                )
            )

        threading.Thread(target=spawn_responder).start()
        with self.assertRaisesRegex(TimeoutError, "wait runtime agent ready timed out"):
            device.spawn("com.demo.target", agent_ready_timeout_ms=20)

        device.close()

    def test_spawn_uses_stable_spawn_response_timeout_budget(self) -> None:
        connection = FakeConnection()
        device = Device(connection, default_timeout_ms=20)

        def spawn_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SPAWN_REQUEST)
            time.sleep(0.03)
            connection.push_incoming(
                Frame(
                    MessageType.SPAWN_RESPONSE,
                    request_frame.message_id,
                    encode_spawn_response(SpawnResponse(pid=4321, error=ErrorInfo())),
                )
            )
            connection.push_incoming(
                Frame(
                    MessageType.AGENT_READY,
                    104,
                    encode_agent_ready(
                        AgentReady(
                            pid=4321,
                            process_name="com.demo.target",
                            spawn_token="spawn-token-delayed-response",
                            arch="arm64",
                            version="0.1.0",
                        )
                    ),
                )
            )

        threading.Thread(target=spawn_responder).start()
        session = device.spawn("com.demo.target", agent_ready_timeout_ms=20)
        self.assertEqual(session.pid, 4321)
        self.assertEqual(session.process_name, "com.demo.target")

        device.close()

    def test_spawn_ignores_control_stage_agent_ready(self) -> None:
        connection = FakeConnection()
        device = Device(connection, default_timeout_ms=1000)

        def spawn_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SPAWN_REQUEST)
            connection.push_incoming(
                Frame(
                    MessageType.SPAWN_RESPONSE,
                    request_frame.message_id,
                    encode_spawn_response(SpawnResponse(pid=4321, error=ErrorInfo())),
                )
            )
            connection.push_incoming(
                Frame(
                    MessageType.AGENT_READY,
                    99,
                    encode_agent_ready(
                        AgentReady(
                            pid=4321,
                            process_name="com.demo.target",
                            spawn_token="spawn-token-control-only",
                            arch="arm64",
                            version="0.1.0",
                            stage=AgentReadyStage.CONTROL,
                        )
                    ),
                )
            )

        threading.Thread(target=spawn_responder).start()
        with self.assertRaisesRegex(TimeoutError, "wait runtime agent ready timed out"):
            device.spawn("com.demo.target", agent_ready_timeout_ms=20)

        device.close()

    def test_spawn_ignores_mismatched_runtime_agent_ready_for_same_pid(self) -> None:
        connection = FakeConnection()
        device = Device(connection, default_timeout_ms=1000)

        def spawn_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SPAWN_REQUEST)
            connection.push_incoming(
                Frame(
                    MessageType.SPAWN_RESPONSE,
                    request_frame.message_id,
                    encode_spawn_response(SpawnResponse(pid=4321, error=ErrorInfo())),
                )
            )
            connection.push_incoming(
                Frame(
                    MessageType.AGENT_READY,
                    100,
                    encode_agent_ready(
                        AgentReady(
                            pid=4321,
                            process_name="com.demo.other",
                            spawn_token="spawn-token-wrong-name",
                            arch="arm64",
                            version="0.1.0",
                        )
                    ),
                )
            )
            connection.push_incoming(
                Frame(
                    MessageType.AGENT_READY,
                    101,
                    encode_agent_ready(
                        AgentReady(
                            pid=4321,
                            process_name="com.demo.target",
                            spawn_token="spawn-token-correct-name",
                            arch="arm64",
                            version="0.1.0",
                        )
                    ),
                )
            )

        threading.Thread(target=spawn_responder).start()
        session = device.spawn("com.demo.target")
        self.assertEqual(session.pid, 4321)
        self.assertEqual(session.process_name, "com.demo.target")

        device.close()

    def test_spawn_ignores_runtime_agent_ready_delivered_before_spawn_response(self) -> None:
        connection = FakeConnection()
        device = Device(connection, default_timeout_ms=1000)

        def spawn_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SPAWN_REQUEST)
            connection.push_incoming(
                Frame(
                    MessageType.AGENT_READY,
                    102,
                    encode_agent_ready(
                        AgentReady(
                            pid=4321,
                            process_name="com.demo.target",
                            spawn_token="spawn-token-early-runtime",
                            arch="arm64",
                            version="0.1.0",
                        )
                    ),
                )
            )
            connection.push_incoming(
                Frame(
                    MessageType.SPAWN_RESPONSE,
                    request_frame.message_id,
                    encode_spawn_response(SpawnResponse(pid=4321, error=ErrorInfo())),
                )
            )
            connection.push_incoming(
                Frame(
                    MessageType.AGENT_READY,
                    103,
                    encode_agent_ready(
                        AgentReady(
                            pid=4321,
                            process_name="com.demo.target",
                            spawn_token="spawn-token-current-runtime",
                            arch="arm64",
                            version="0.1.0",
                        )
                    ),
                )
            )

        threading.Thread(target=spawn_responder).start()
        session = device.spawn("com.demo.target")
        self.assertEqual(session.pid, 4321)
        self.assertEqual(session.process_name, "com.demo.target")

        device.close()

    def test_list_apps_and_processes(self) -> None:
        connection = FakeConnection()
        device = Device(connection, default_timeout_ms=1000)

        def respond_apps() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.APP_LIST_REQ)
            connection.push_incoming(
                Frame(
                    MessageType.APP_LIST_RESP,
                    request_frame.message_id,
                    encode_app_list_response(
                        AppListResponse(
                            apps=[
                                AppEntry(package_name="com.android.systemui"),
                                AppEntry(package_name="com.demo.target"),
                            ],
                            error=ErrorInfo(),
                        )
                    ),
                )
            )

        threading.Thread(target=respond_apps).start()
        apps = device.enumerate_apps()
        self.assertEqual([app.package_name for app in apps], ["com.android.systemui", "com.demo.target"])

        def respond_processes() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.PROCESS_LIST_REQ)
            connection.push_incoming(
                Frame(
                    MessageType.PROCESS_LIST_RESP,
                    request_frame.message_id,
                    encode_process_list_response(
                        ProcessListResponse(
                            processes=[
                                ProcessEntry(pid=123, name="system_server"),
                                ProcessEntry(pid=456, name="com.demo.target"),
                            ],
                            error=ErrorInfo(),
                        )
                    ),
                )
            )

        threading.Thread(target=respond_processes).start()
        processes = device.enumerate_processes()
        self.assertEqual([proc.pid for proc in processes], [123, 456])

    def test_spawn_create_load_post_unload_and_resume(self) -> None:
        connection = FakeConnection()
        device = Device(connection, default_timeout_ms=1000)

        def spawn_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SPAWN_REQUEST)
            request = decode_spawn_request(request_frame.payload)
            self.assertEqual(request.identifier, "com.demo.target")
            connection.push_incoming(
                Frame(
                    MessageType.SPAWN_RESPONSE,
                    request_frame.message_id,
                    encode_spawn_response(SpawnResponse(pid=4321, error=ErrorInfo())),
                )
            )
            connection.push_incoming(
                Frame(
                    MessageType.AGENT_READY,
                    99,
                    encode_agent_ready(
                        AgentReady(
                            pid=4321,
                            process_name="com.demo.target",
                            spawn_token="spawn-token-1",
                            arch="arm64",
                            version="0.1.0",
                        )
                    ),
                )
            )

        threading.Thread(target=spawn_responder).start()
        session = device.spawn("com.demo.target")
        self.assertEqual(session.pid, 4321)
        self.assertEqual(session.process_name, "com.demo.target")

        script = session.create_script("send({type:'send',payload:'hello'})", name="smoke.js")
        self.assertEqual(script.name, "smoke.js")

        def create_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SCRIPT_CREATE)
            request = decode_script_create(request_frame.payload)
            self.assertEqual(request.source, "send({type:'send',payload:'hello'})")
            self.assertEqual(request.name, "smoke.js")
            self.assertEqual(request.session_id, session.session_id)
            connection.push_incoming(
                Frame(
                    MessageType.SCRIPT_CREATE_RESP,
                    request_frame.message_id,
                    encode_script_create_response(
                        ScriptCreateResponse(script_id=1000, success=True, error=ErrorInfo())
                    ),
                )
            )

        threading.Thread(target=create_responder).start()
        script_id = script.create()
        self.assertEqual(script_id, 1000)

        def load_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SCRIPT_LOAD)
            request = decode_script_load(request_frame.payload)
            self.assertEqual(request.script_id, 1000)
            connection.push_incoming(
                Frame(
                    MessageType.SCRIPT_LOAD_RESP,
                    request_frame.message_id,
                    encode_script_response(
                        ScriptResponse(script_id=1000, success=True, error=ErrorInfo())
                    ),
                )
            )
            connection.push_incoming(
                Frame(
                    MessageType.SCRIPT_MESSAGE,
                    77,
                    encode_script_message(
                        ScriptMessage(
                            script_id=1000,
                            message='{"type":"send","payload":"script-loaded"}',
                            data=b"",
                        )
                    ),
                )
            )

        threading.Thread(target=load_responder).start()
        script.load()
        message = script.wait_for_message()
        self.assertEqual(message.message, '{"type":"send","payload":"script-loaded"}')

        script.post('{"type":"post","payload":"hello-from-host"}', data=b"\x13\x37")
        sent_post = connection.wait_for_sent_frame(MessageType.SCRIPT_POST)
        post = decode_script_post(sent_post.payload)
        self.assertEqual(post.script_id, 1000)
        self.assertEqual(post.message, '{"type":"post","payload":"hello-from-host"}')
        self.assertEqual(post.data, b"\x13\x37")

        def unload_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SCRIPT_UNLOAD)
            request = decode_script_unload(request_frame.payload)
            self.assertEqual(request.script_id, 1000)
            connection.push_incoming(
                Frame(
                    MessageType.SCRIPT_UNLOAD_RESP,
                    request_frame.message_id,
                    encode_script_response(
                        ScriptResponse(script_id=1000, success=True, error=ErrorInfo())
                    ),
                )
            )

        threading.Thread(target=unload_responder).start()
        script.unload()

        def resume_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.RESUME_REQUEST)
            request = decode_resume_request(request_frame.payload)
            self.assertEqual(request.pid, 4321)
            connection.push_incoming(
                Frame(
                    MessageType.RESUME_RESPONSE,
                    request_frame.message_id,
                    encode_resume_response(ResumeResponse(pid=4321, error=ErrorInfo())),
                )
            )

        threading.Thread(target=resume_responder).start()
        device.resume(4321)

    def test_script_call_round_trip(self) -> None:
        connection = FakeConnection()
        device = Device(connection, default_timeout_ms=1000)

        def spawn_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SPAWN_REQUEST)
            connection.push_incoming(
                Frame(
                    MessageType.SPAWN_RESPONSE,
                    request_frame.message_id,
                    encode_spawn_response(SpawnResponse(pid=4321, error=ErrorInfo())),
                )
            )
            connection.push_incoming(
                Frame(
                    MessageType.AGENT_READY,
                    99,
                    encode_agent_ready(
                        AgentReady(
                            pid=4321,
                            process_name="com.demo.target",
                            spawn_token="spawn-token-2",
                            arch="arm64",
                            version="0.1.0",
                        )
                    ),
                )
            )

        threading.Thread(target=spawn_responder).start()
        session = device.spawn("com.demo.target")
        script = session.create_script("rpc.exports = { ping(name) { return 'hello ' + name; } }")
        script.script_id = 1000

        def rpc_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.RPC_REQUEST)
            connection.push_incoming(
                Frame(
                    MessageType.RPC_RESPONSE,
                    request_frame.message_id,
                    encode_rpc_response(
                        RpcResponse(
                            script_id=1000,
                            success=True,
                            result_json='"hello world"',
                            error=ErrorInfo(),
                        )
                    ),
                )
            )

        threading.Thread(target=rpc_responder).start()
        result = script.call("ping", "world")
        self.assertEqual(result, "hello world")

    def test_script_message_callback(self) -> None:
        connection = FakeConnection()
        device = Device(connection, default_timeout_ms=1000)

        def spawn_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SPAWN_REQUEST)
            connection.push_incoming(
                Frame(
                    MessageType.SPAWN_RESPONSE,
                    request_frame.message_id,
                    encode_spawn_response(SpawnResponse(pid=4321, error=ErrorInfo())),
                )
            )
            connection.push_incoming(
                Frame(
                    MessageType.AGENT_READY,
                    99,
                    encode_agent_ready(
                        AgentReady(
                            pid=4321,
                            process_name="com.demo.target",
                            spawn_token="spawn-token-3",
                            arch="arm64",
                            version="0.1.0",
                        )
                    ),
                )
            )

        threading.Thread(target=spawn_responder).start()
        session = device.spawn("com.demo.target")
        script = session.create_script("console.log('ok')", name="callback.js")

        def create_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SCRIPT_CREATE)
            connection.push_incoming(
                Frame(
                    MessageType.SCRIPT_CREATE_RESP,
                    request_frame.message_id,
                    encode_script_create_response(
                        ScriptCreateResponse(script_id=1000, success=True, error=ErrorInfo())
                    ),
                )
            )

        threading.Thread(target=create_responder).start()
        script.create()

        messages = []
        callback_event = threading.Event()

        def on_message(message, data) -> None:
            messages.append((message, data))
            callback_event.set()

        script.on("message", on_message)

        def load_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SCRIPT_LOAD)
            connection.push_incoming(
                Frame(
                    MessageType.SCRIPT_MESSAGE,
                    77,
                    encode_script_message(
                        ScriptMessage(
                            script_id=1000,
                            message='{"type":"send","payload":"callback-fired"}',
                            data=b"\x01\x02",
                        )
                    ),
                )
            )
            time.sleep(0.02)
            connection.push_incoming(
                Frame(
                    MessageType.SCRIPT_LOAD_RESP,
                    request_frame.message_id,
                    encode_script_response(
                        ScriptResponse(script_id=1000, success=True, error=ErrorInfo())
                    ),
                )
            )

        threading.Thread(target=load_responder).start()
        script.load()

        self.assertTrue(callback_event.wait(1.0))
        self.assertEqual(len(messages), 1)
        self.assertEqual(messages[0][0].message, '{"type":"send","payload":"callback-fired"}')
        self.assertEqual(messages[0][1], b"\x01\x02")

        script.off("message", on_message)

    def test_unload_clears_script_id_and_prevents_further_use(self) -> None:
        connection = FakeConnection()
        device = Device(connection, default_timeout_ms=1000)

        def spawn_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SPAWN_REQUEST)
            connection.push_incoming(
                Frame(
                    MessageType.SPAWN_RESPONSE,
                    request_frame.message_id,
                    encode_spawn_response(SpawnResponse(pid=4321, error=ErrorInfo())),
                )
            )
            connection.push_incoming(
                Frame(
                    MessageType.AGENT_READY,
                    99,
                    encode_agent_ready(
                        AgentReady(
                            pid=4321,
                            process_name="com.demo.target",
                            spawn_token="spawn-token-unload-reset",
                            arch="arm64",
                            version="0.1.0",
                        )
                    ),
                )
            )

        threading.Thread(target=spawn_responder).start()
        session = device.spawn("com.demo.target")
        script = session.create_script("console.log('ok')", name="unload-reset.js")

        def create_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SCRIPT_CREATE)
            connection.push_incoming(
                Frame(
                    MessageType.SCRIPT_CREATE_RESP,
                    request_frame.message_id,
                    encode_script_create_response(
                        ScriptCreateResponse(script_id=1000, success=True, error=ErrorInfo())
                    ),
                )
            )

        threading.Thread(target=create_responder).start()
        self.assertEqual(script.create(), 1000)

        def unload_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SCRIPT_UNLOAD)
            request = decode_script_unload(request_frame.payload)
            self.assertEqual(request.script_id, 1000)
            connection.push_incoming(
                Frame(
                    MessageType.SCRIPT_UNLOAD_RESP,
                    request_frame.message_id,
                    encode_script_response(
                        ScriptResponse(script_id=1000, success=True, error=ErrorInfo())
                    ),
                )
            )

        threading.Thread(target=unload_responder).start()
        script.unload()
        self.assertIsNone(script.script_id)

        with self.assertRaisesRegex(ValueError, "script has not been created yet"):
            script.post('{"type":"post","payload":"after-unload"}')
        with self.assertRaisesRegex(ValueError, "script has not been created yet"):
            script.call("ping")
        with self.assertRaisesRegex(ValueError, "script has not been created yet"):
            script.wait_for_message(timeout_ms=20)

        device.close()

    def test_unload_removes_registered_message_callbacks_for_old_script_id(self) -> None:
        connection = FakeConnection()
        device = Device(connection, default_timeout_ms=1000)

        def spawn_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SPAWN_REQUEST)
            connection.push_incoming(
                Frame(
                    MessageType.SPAWN_RESPONSE,
                    request_frame.message_id,
                    encode_spawn_response(SpawnResponse(pid=4321, error=ErrorInfo())),
                )
            )
            connection.push_incoming(
                Frame(
                    MessageType.AGENT_READY,
                    100,
                    encode_agent_ready(
                        AgentReady(
                            pid=4321,
                            process_name="com.demo.target",
                            spawn_token="spawn-token-unload-callback",
                            arch="arm64",
                            version="0.1.0",
                        )
                    ),
                )
            )

        threading.Thread(target=spawn_responder).start()
        session = device.spawn("com.demo.target")
        script = session.create_script("console.log('cb')", name="unload-callback.js")

        callback_messages = []

        def on_message(message, data) -> None:
            callback_messages.append((message.message, data))

        script.on("message", on_message)

        def create_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SCRIPT_CREATE)
            connection.push_incoming(
                Frame(
                    MessageType.SCRIPT_CREATE_RESP,
                    request_frame.message_id,
                    encode_script_create_response(
                        ScriptCreateResponse(script_id=1000, success=True, error=ErrorInfo())
                    ),
                )
            )

        threading.Thread(target=create_responder).start()
        self.assertEqual(script.create(), 1000)

        def unload_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SCRIPT_UNLOAD)
            connection.push_incoming(
                Frame(
                    MessageType.SCRIPT_UNLOAD_RESP,
                    request_frame.message_id,
                    encode_script_response(
                        ScriptResponse(script_id=1000, success=True, error=ErrorInfo())
                    ),
                )
            )

        threading.Thread(target=unload_responder).start()
        script.unload()

        connection.push_incoming(
            Frame(
                MessageType.SCRIPT_MESSAGE,
                101,
                encode_script_message(
                    ScriptMessage(
                        script_id=1000,
                        message='{"type":"send","payload":"stale-after-unload"}',
                        data=b"\x09",
                    )
                ),
            )
        )
        time.sleep(0.05)

        self.assertEqual(callback_messages, [])
        device.close()

    def test_spawn_tolerates_socket_timeout_in_reader_loop(self) -> None:
        connection = TimeoutThenFrameConnection()
        device = Device(connection, default_timeout_ms=1000)

        def spawn_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SPAWN_REQUEST)
            connection.push_incoming(
                Frame(
                    MessageType.SPAWN_RESPONSE,
                    request_frame.message_id,
                    encode_spawn_response(SpawnResponse(pid=4321, error=ErrorInfo())),
                )
            )
            connection.push_incoming(
                Frame(
                    MessageType.AGENT_READY,
                    99,
                    encode_agent_ready(
                        AgentReady(
                            pid=4321,
                            process_name="com.demo.target",
                            spawn_token="spawn-token-4",
                            arch="arm64",
                            version="0.1.0",
                        )
                    ),
                )
            )

        threading.Thread(target=spawn_responder).start()
        session = device.spawn("com.demo.target")
        self.assertEqual(session.pid, 4321)
        self.assertEqual(session.process_name, "com.demo.target")
        device.close()

    def test_spawn_clears_stale_agent_ready_events_before_new_transaction(self) -> None:
        connection = FakeConnection()
        device = Device(connection, default_timeout_ms=1000)

        with device._state_cv:
            device._agent_ready_events.append(
                AgentReady(
                    pid=4321,
                    process_name="com.demo.target",
                    spawn_token="stale-spawn-token",
                    arch="arm64",
                    version="0.1.0",
                )
            )

        def spawn_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SPAWN_REQUEST)
            connection.push_incoming(
                Frame(
                    MessageType.SPAWN_RESPONSE,
                    request_frame.message_id,
                    encode_spawn_response(SpawnResponse(pid=4321, error=ErrorInfo())),
                )
            )

        threading.Thread(target=spawn_responder).start()
        with self.assertRaisesRegex(TimeoutError, "wait runtime agent ready timed out"):
            device.spawn("com.demo.target", agent_ready_timeout_ms=20)

        device.close()

    def test_spawn_clears_stale_script_messages_before_new_transaction(self) -> None:
        connection = FakeConnection()
        device = Device(connection, default_timeout_ms=1000)

        with device._state_cv:
            device._script_messages.append(
                ScriptMessage(
                    script_id=1000,
                    message='{"type":"send","payload":"stale-script-message"}',
                    data=b"\x01",
                )
            )

        def spawn_responder() -> None:
            request_frame = connection.wait_for_sent_frame(MessageType.SPAWN_REQUEST)
            connection.push_incoming(
                Frame(
                    MessageType.SPAWN_RESPONSE,
                    request_frame.message_id,
                    encode_spawn_response(SpawnResponse(pid=5001, error=ErrorInfo())),
                )
            )
            connection.push_incoming(
                Frame(
                    MessageType.AGENT_READY,
                    301,
                    encode_agent_ready(
                        AgentReady(
                            pid=5001,
                            process_name="com.demo.target",
                            spawn_token="spawn-token-fresh",
                            arch="arm64",
                            version="0.1.0",
                        )
                    ),
                )
            )
            time.sleep(0.02)
            connection.push_incoming(
                Frame(
                    MessageType.SCRIPT_MESSAGE,
                    302,
                    encode_script_message(
                        ScriptMessage(
                            script_id=2000,
                            message='{"type":"send","payload":"fresh-script-message"}',
                            data=b"\x02",
                        )
                    ),
                )
            )

        threading.Thread(target=spawn_responder).start()
        session = device.spawn("com.demo.target")
        self.assertEqual(session.pid, 5001)

        message = device.wait_for_script_message(timeout_ms=1000, script_id=None)
        self.assertEqual(message.message, '{"type":"send","payload":"fresh-script-message"}')
        self.assertEqual(message.data, b"\x02")

        device.close()


if __name__ == "__main__":
    unittest.main()
