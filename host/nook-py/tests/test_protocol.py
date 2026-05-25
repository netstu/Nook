import os
import sys
import unittest


TEST_ROOT = os.path.dirname(__file__)
PACKAGE_ROOT = os.path.abspath(os.path.join(TEST_ROOT, ".."))
if PACKAGE_ROOT not in sys.path:
    sys.path.insert(0, PACKAGE_ROOT)


from nook.protocol import (  # noqa: E402
    AppEntry,
    AppListResponse,
    AttachResponse,
    ErrorInfo,
    Frame,
    MessageType,
    ProcessEntry,
    ProcessListResponse,
    ResumeResponse,
    ScriptCreate,
    ScriptCreateResponse,
    ScriptLoad,
    ScriptMessage,
    RpcRequest,
    RpcResponse,
    ScriptResponse,
    SpawnRequest,
    SpawnResponse,
    TlvDecoder,
    TlvEncoder,
    decode_app_list_response,
    decode_attach_response,
    decode_process_list_response,
    decode_resume_response,
    decode_script_create,
    decode_script_create_response,
    decode_script_load,
    decode_script_message,
    decode_rpc_request,
    decode_rpc_response,
    decode_script_response,
    decode_spawn_request,
    decode_spawn_response,
    encode_app_list_response,
    encode_attach_response,
    encode_process_list_response,
    encode_resume_response,
    encode_script_create,
    encode_script_create_response,
    encode_script_load,
    encode_script_message,
    encode_rpc_request,
    encode_rpc_response,
    encode_script_response,
    encode_spawn_request,
    encode_spawn_response,
)


class ProtocolTests(unittest.TestCase):
    def test_frame_round_trip(self) -> None:
        frame = Frame(MessageType.SCRIPT_LOAD, 0x10203040, b"\x11\x22\x33")
        encoded = frame.serialize()

        parsed, consumed = Frame.parse(encoded)

        self.assertEqual(consumed, len(encoded))
        self.assertEqual(parsed.message_type, MessageType.SCRIPT_LOAD)
        self.assertEqual(parsed.message_id, 0x10203040)
        self.assertEqual(parsed.payload, b"\x11\x22\x33")

    def test_frame_rejects_oversized_payload(self) -> None:
        header = ((Frame.MAX_PAYLOAD_SIZE + 1).to_bytes(4, "big") +
                  int(MessageType.SCRIPT_MESSAGE).to_bytes(2, "big") +
                  (7).to_bytes(4, "big"))

        with self.assertRaises(ValueError):
            Frame.parse(header)

    def test_tlv_round_trip(self) -> None:
        nested = TlvEncoder()
        nested.put_string(1, "child")

        encoder = TlvEncoder()
        encoder.put_uint32(1, 123456)
        encoder.put_string(2, "hello")
        encoder.put_bytes(3, b"\x01\x02\x03")
        encoder.put_nested(4, nested)

        decoder = TlvDecoder(encoder.build())

        self.assertEqual(decoder.get_uint32(1), 123456)
        self.assertEqual(decoder.get_string(2), "hello")
        self.assertEqual(decoder.get_bytes(3), b"\x01\x02\x03")
        child = decoder.get_nested(4)
        self.assertEqual(child.get_string(1), "child")

    def test_spawn_request_round_trip(self) -> None:
        request = SpawnRequest(identifier="com.demo.target", argv=["--debug", "--wait"])

        parsed = decode_spawn_request(encode_spawn_request(request))

        self.assertEqual(parsed.identifier, request.identifier)
        self.assertEqual(parsed.argv, request.argv)

    def test_spawn_response_round_trip(self) -> None:
        response = SpawnResponse(pid=42, error=ErrorInfo(code=-7, message="spawn failed"))

        parsed = decode_spawn_response(encode_spawn_response(response))

        self.assertEqual(parsed.pid, 42)
        self.assertEqual(parsed.error.code, -7)
        self.assertEqual(parsed.error.message, "spawn failed")

    def test_attach_response_round_trip(self) -> None:
        response = AttachResponse(
            session_id=7,
            pid=1357,
            process_name="com.demo.target",
            error=ErrorInfo(code=-1, message="inject failed"),
        )

        parsed = decode_attach_response(encode_attach_response(response))

        self.assertEqual(parsed.session_id, 7)
        self.assertEqual(parsed.pid, 1357)
        self.assertEqual(parsed.process_name, "com.demo.target")
        self.assertEqual(parsed.error.message, "inject failed")

    def test_resume_response_round_trip(self) -> None:
        response = ResumeResponse(pid=2100, error=ErrorInfo(code=-3, message="not suspended"))

        parsed = decode_resume_response(encode_resume_response(response))

        self.assertEqual(parsed.pid, 2100)
        self.assertEqual(parsed.error.code, -3)
        self.assertEqual(parsed.error.message, "not suspended")

    def test_script_messages_round_trip(self) -> None:
        create = ScriptCreate(session_id=9, source="console.log('ok')", name="hello.js")
        parsed_create = decode_script_create(encode_script_create(create))
        self.assertEqual(parsed_create.session_id, 9)
        self.assertEqual(parsed_create.source, "console.log('ok')")
        self.assertEqual(parsed_create.name, "hello.js")

        create_response = ScriptCreateResponse(
            script_id=13,
            success=False,
            error=ErrorInfo(code=-3, message="compile error"),
        )
        parsed_create_response = decode_script_create_response(
            encode_script_create_response(create_response)
        )
        self.assertEqual(parsed_create_response.script_id, 13)
        self.assertFalse(parsed_create_response.success)
        self.assertEqual(parsed_create_response.error.message, "compile error")

        load = ScriptLoad(script_id=101)
        parsed_load = decode_script_load(encode_script_load(load))
        self.assertEqual(parsed_load.script_id, 101)

        response = ScriptResponse(
            script_id=101,
            success=False,
            error=ErrorInfo(code=-4, message="load failed"),
        )
        parsed_response = decode_script_response(encode_script_response(response))
        self.assertEqual(parsed_response.script_id, 101)
        self.assertFalse(parsed_response.success)
        self.assertEqual(parsed_response.error.message, "load failed")

        message = ScriptMessage(
            script_id=101,
            message='{"type":"log","payload":"ok"}',
            data=b"\x41\x42",
        )
        parsed_message = decode_script_message(encode_script_message(message))
        self.assertEqual(parsed_message.script_id, 101)
        self.assertEqual(parsed_message.message, '{"type":"log","payload":"ok"}')
        self.assertEqual(parsed_message.data, b"\x41\x42")

    def test_rpc_round_trip(self) -> None:
        request = RpcRequest(script_id=7, method="ping", args_json='["hello",123]')
        parsed_request = decode_rpc_request(encode_rpc_request(request))
        self.assertEqual(parsed_request.script_id, 7)
        self.assertEqual(parsed_request.method, "ping")
        self.assertEqual(parsed_request.args_json, '["hello",123]')

        response = RpcResponse(
            script_id=7,
            success=True,
            result_json='{"value":"pong"}',
            error=ErrorInfo(),
        )
        parsed_response = decode_rpc_response(encode_rpc_response(response))
        self.assertEqual(parsed_response.script_id, 7)
        self.assertTrue(parsed_response.success)
        self.assertEqual(parsed_response.result_json, '{"value":"pong"}')

    def test_process_and_app_list_round_trip(self) -> None:
        process_response = ProcessListResponse(
            processes=[
                ProcessEntry(pid=123, name="system_server"),
                ProcessEntry(pid=456, name="com.demo.target"),
            ],
            error=ErrorInfo(),
        )
        parsed_processes = decode_process_list_response(
            encode_process_list_response(process_response)
        )
        self.assertEqual([item.pid for item in parsed_processes.processes], [123, 456])
        self.assertEqual(
            [item.name for item in parsed_processes.processes],
            ["system_server", "com.demo.target"],
        )

        app_response = AppListResponse(
            apps=[
                AppEntry(package_name="com.android.systemui"),
                AppEntry(package_name="com.demo.target"),
            ],
            error=ErrorInfo(),
        )
        parsed_apps = decode_app_list_response(encode_app_list_response(app_response))
        self.assertEqual(
            [item.package_name for item in parsed_apps.apps],
            ["com.android.systemui", "com.demo.target"],
        )


if __name__ == "__main__":
    unittest.main()
