from dataclasses import dataclass, field
from enum import IntEnum
from typing import Dict, List, Optional, Tuple


class MessageType(IntEnum):
    ATTACH_REQUEST = 0x0100
    ATTACH_RESPONSE = 0x0101
    DETACH_REQUEST = 0x0102
    DETACH_RESPONSE = 0x0103
    SPAWN_REQUEST = 0x0104
    SPAWN_RESPONSE = 0x0105
    RESUME_REQUEST = 0x0106
    RESUME_RESPONSE = 0x0107
    SCRIPT_CREATE = 0x0200
    SCRIPT_CREATE_RESP = 0x0201
    SCRIPT_LOAD = 0x0202
    SCRIPT_LOAD_RESP = 0x0203
    SCRIPT_UNLOAD = 0x0204
    SCRIPT_UNLOAD_RESP = 0x0205
    SCRIPT_MESSAGE = 0x0300
    SCRIPT_POST = 0x0301
    RPC_REQUEST = 0x0400
    RPC_RESPONSE = 0x0401
    PROCESS_LIST_REQ = 0x0500
    PROCESS_LIST_RESP = 0x0501
    APP_LIST_REQ = 0x0502
    APP_LIST_RESP = 0x0503
    PING = 0x0600
    PONG = 0x0601
    ERROR = 0x06FF
    AGENT_READY = 0xFF00
    AGENT_SHUTDOWN = 0xFF01


class AgentReadyStage(IntEnum):
    RUNTIME = 0
    CONTROL = 1


@dataclass
class Frame:
    message_type: MessageType
    message_id: int
    payload: bytes = b""

    HEADER_SIZE = 10
    MAX_PAYLOAD_SIZE = 16 * 1024 * 1024

    def serialize(self) -> bytes:
        return (
            len(self.payload).to_bytes(4, "big")
            + int(self.message_type).to_bytes(2, "big")
            + int(self.message_id).to_bytes(4, "big")
            + self.payload
        )

    @classmethod
    def parse(cls, data: bytes) -> Tuple["Frame", int]:
        if len(data) < cls.HEADER_SIZE:
            raise ValueError("frame header incomplete")
        payload_len = int.from_bytes(data[0:4], "big")
        if payload_len > cls.MAX_PAYLOAD_SIZE:
            raise ValueError("payload too large")
        total_len = cls.HEADER_SIZE + payload_len
        if len(data) < total_len:
            raise ValueError("frame payload incomplete")
        return (
            cls(
                message_type=MessageType(int.from_bytes(data[4:6], "big")),
                message_id=int.from_bytes(data[6:10], "big"),
                payload=data[10:total_len],
            ),
            total_len,
        )


class TlvEncoder:
    TYPE_UINT8 = 0x1
    TYPE_UINT16 = 0x2
    TYPE_UINT32 = 0x3
    TYPE_UINT64 = 0x4
    TYPE_STRING = 0x5
    TYPE_BYTES = 0x6
    TYPE_NESTED = 0x7

    def __init__(self) -> None:
        self._parts: List[bytes] = []

    def _put(self, field_id: int, value_type: int, value: bytes) -> None:
        tag = ((field_id & 0x0F) << 4) | (value_type & 0x0F)
        self._parts.append(bytes((tag,)) + len(value).to_bytes(2, "big") + value)

    def put_uint8(self, field_id: int, value: int) -> None:
        self._put(field_id, self.TYPE_UINT8, value.to_bytes(1, "big"))

    def put_uint32(self, field_id: int, value: int) -> None:
        self._put(field_id, self.TYPE_UINT32, value.to_bytes(4, "big"))

    def put_string(self, field_id: int, value: str) -> None:
        self._put(field_id, self.TYPE_STRING, value.encode("utf-8"))

    def put_bytes(self, field_id: int, value: bytes) -> None:
        self._put(field_id, self.TYPE_BYTES, value)

    def put_nested(self, field_id: int, nested: "TlvEncoder") -> None:
        self._put(field_id, self.TYPE_NESTED, nested.build())

    def build(self) -> bytes:
        return b"".join(self._parts)


class TlvDecoder:
    def __init__(self, data: bytes) -> None:
        self._fields: Dict[int, List[Tuple[int, bytes]]] = {}
        offset = 0
        while offset < len(data):
            if offset + 3 > len(data):
                raise ValueError("invalid tlv header")
            tag = data[offset]
            field_id = (tag >> 4) & 0x0F
            value_type = tag & 0x0F
            length = int.from_bytes(data[offset + 1:offset + 3], "big")
            start = offset + 3
            end = start + length
            if end > len(data):
                raise ValueError("invalid tlv length")
            self._fields.setdefault(field_id, []).append((value_type, data[start:end]))
            offset = end

    def _get(self, field_id: int, expected_type: int) -> Optional[bytes]:
        for value_type, value in self._fields.get(field_id, []):
            if value_type == expected_type:
                return value
        return None

    def _get_all(self, field_id: int, expected_type: int) -> List[bytes]:
        return [value for value_type, value in self._fields.get(field_id, []) if value_type == expected_type]

    def get_uint8(self, field_id: int, default: Optional[int] = None) -> Optional[int]:
        value = self._get(field_id, TlvEncoder.TYPE_UINT8)
        return default if value is None else int.from_bytes(value, "big")

    def get_uint32(self, field_id: int, default: Optional[int] = None) -> Optional[int]:
        value = self._get(field_id, TlvEncoder.TYPE_UINT32)
        return default if value is None else int.from_bytes(value, "big")

    def get_string(self, field_id: int, default: Optional[str] = None) -> Optional[str]:
        value = self._get(field_id, TlvEncoder.TYPE_STRING)
        return default if value is None else value.decode("utf-8")

    def get_bytes(self, field_id: int, default: Optional[bytes] = None) -> Optional[bytes]:
        value = self._get(field_id, TlvEncoder.TYPE_BYTES)
        return default if value is None else value

    def get_nested(self, field_id: int) -> "TlvDecoder":
        value = self._get(field_id, TlvEncoder.TYPE_NESTED)
        if value is None:
            raise KeyError(field_id)
        return TlvDecoder(value)

    def get_all_nested(self, field_id: int) -> List["TlvDecoder"]:
        return [TlvDecoder(value) for value in self._get_all(field_id, TlvEncoder.TYPE_NESTED)]


@dataclass
class ErrorInfo:
    code: int = 0
    message: str = ""


@dataclass
class SpawnRequest:
    identifier: str = ""
    argv: List[str] = field(default_factory=list)


@dataclass
class AttachRequest:
    pid: int = 0
    identifier: str = ""


@dataclass
class AttachResponse:
    session_id: int = 0
    pid: int = 0
    process_name: str = ""
    error: ErrorInfo = field(default_factory=ErrorInfo)


@dataclass
class SpawnResponse:
    pid: int = 0
    error: ErrorInfo = field(default_factory=ErrorInfo)


@dataclass
class DetachRequest:
    session_id: int = 0


@dataclass
class DetachResponse:
    session_id: int = 0
    error: ErrorInfo = field(default_factory=ErrorInfo)


@dataclass
class ResumeRequest:
    pid: int = 0


@dataclass
class ResumeResponse:
    pid: int = 0
    error: ErrorInfo = field(default_factory=ErrorInfo)


@dataclass
class AgentReady:
    pid: int = 0
    process_name: str = ""
    spawn_token: str = ""
    arch: str = ""
    version: str = ""
    stage: AgentReadyStage = AgentReadyStage.RUNTIME


@dataclass
class ScriptCreate:
    session_id: int = 0
    source: str = ""
    name: str = ""


@dataclass
class ScriptCreateResponse:
    script_id: int = 0
    success: bool = False
    error: ErrorInfo = field(default_factory=ErrorInfo)


@dataclass
class ScriptResponse:
    script_id: int = 0
    success: bool = False
    error: ErrorInfo = field(default_factory=ErrorInfo)


@dataclass
class ScriptLoad:
    script_id: int = 0


@dataclass
class ScriptUnload:
    script_id: int = 0


@dataclass
class ScriptMessage:
    script_id: int = 0
    message: str = ""
    data: bytes = b""


@dataclass
class ScriptPost:
    script_id: int = 0
    message: str = ""
    data: bytes = b""


@dataclass
class RpcRequest:
    script_id: int = 0
    method: str = ""
    args_json: str = ""


@dataclass
class RpcResponse:
    script_id: int = 0
    success: bool = False
    result_json: str = ""
    error: ErrorInfo = field(default_factory=ErrorInfo)


@dataclass
class ProcessEntry:
    pid: int = 0
    name: str = ""


@dataclass
class ProcessListResponse:
    processes: List[ProcessEntry] = field(default_factory=list)
    error: ErrorInfo = field(default_factory=ErrorInfo)


@dataclass
class AppEntry:
    package_name: str = ""


@dataclass
class AppListResponse:
    apps: List[AppEntry] = field(default_factory=list)
    error: ErrorInfo = field(default_factory=ErrorInfo)


FIELD_ERROR_CODE = 1
FIELD_ERROR_MESSAGE = 2
FIELD_SPAWN_IDENTIFIER = 1
FIELD_SPAWN_ARGV_BASE = 2
FIELD_ATTACH_PID = 1
FIELD_ATTACH_IDENTIFIER = 2
FIELD_ATTACH_RESP_SESSION_ID = 1
FIELD_ATTACH_RESP_PID = 2
FIELD_ATTACH_RESP_PROCESS_NAME = 3
FIELD_ATTACH_RESP_ERROR = 15
FIELD_SPAWN_RESPONSE_PID = 1
FIELD_SPAWN_RESPONSE_ERROR = 15
FIELD_DETACH_SESSION_ID = 1
FIELD_DETACH_RESP_SESSION_ID = 1
FIELD_DETACH_RESP_ERROR = 15
FIELD_RESUME_PID = 1
FIELD_RESUME_RESP_PID = 1
FIELD_RESUME_RESP_ERROR = 15
FIELD_AGENT_READY_PID = 1
FIELD_AGENT_READY_PROCESS_NAME = 2
FIELD_AGENT_READY_SPAWN_TOKEN = 3
FIELD_AGENT_READY_ARCH = 4
FIELD_AGENT_READY_VERSION = 5
FIELD_AGENT_READY_STAGE = 6
FIELD_SCRIPT_CREATE_SESSION_ID = 1
FIELD_SCRIPT_CREATE_SOURCE = 2
FIELD_SCRIPT_CREATE_NAME = 3
FIELD_SCRIPT_CREATE_RESP_SCRIPT_ID = 1
FIELD_SCRIPT_CREATE_RESP_SUCCESS = 2
FIELD_SCRIPT_CREATE_RESP_ERROR = 15
FIELD_SCRIPT_LOAD_SCRIPT_ID = 1
FIELD_SCRIPT_UNLOAD_SCRIPT_ID = 1
FIELD_SCRIPT_MESSAGE_SCRIPT_ID = 1
FIELD_SCRIPT_MESSAGE_MESSAGE = 2
FIELD_SCRIPT_MESSAGE_DATA = 3
FIELD_RPC_SCRIPT_ID = 1
FIELD_RPC_METHOD = 2
FIELD_RPC_ARGS_JSON = 3
FIELD_RPC_SUCCESS = 2
FIELD_RPC_RESULT_JSON = 3
FIELD_RPC_ERROR = 15
FIELD_PROCESS_LIST_ENTRY = 1
FIELD_PROCESS_ENTRY_PID = 1
FIELD_PROCESS_ENTRY_NAME = 2
FIELD_PROCESS_LIST_ERROR = 15
FIELD_APP_LIST_ENTRY = 1
FIELD_APP_ENTRY_PACKAGE_NAME = 1
FIELD_APP_LIST_ERROR = 15


def _encode_error(error: ErrorInfo) -> TlvEncoder:
    encoder = TlvEncoder()
    encoder.put_uint32(FIELD_ERROR_CODE, error.code & 0xFFFFFFFF)
    if error.message:
        encoder.put_string(FIELD_ERROR_MESSAGE, error.message)
    return encoder


def _decode_error(decoder: TlvDecoder) -> ErrorInfo:
    code = decoder.get_uint32(FIELD_ERROR_CODE, 0) or 0
    if code & 0x80000000:
        code -= 1 << 32
    return ErrorInfo(code=code, message=decoder.get_string(FIELD_ERROR_MESSAGE, "") or "")


def encode_spawn_request(request: SpawnRequest) -> bytes:
    encoder = TlvEncoder()
    if request.identifier:
        encoder.put_string(FIELD_SPAWN_IDENTIFIER, request.identifier)
    for index, value in enumerate(request.argv[:14]):
        encoder.put_string(FIELD_SPAWN_ARGV_BASE + index, value)
    return encoder.build()


def decode_spawn_request(data: bytes) -> SpawnRequest:
    decoder = TlvDecoder(data)
    argv = []
    for field_id in range(FIELD_SPAWN_ARGV_BASE, 16):
        value = decoder.get_string(field_id)
        if value is not None:
            argv.append(value)
    return SpawnRequest(identifier=decoder.get_string(FIELD_SPAWN_IDENTIFIER, "") or "", argv=argv)


def encode_attach_request(request: AttachRequest) -> bytes:
    encoder = TlvEncoder()
    if request.pid:
        encoder.put_uint32(FIELD_ATTACH_PID, request.pid)
    if request.identifier:
        encoder.put_string(FIELD_ATTACH_IDENTIFIER, request.identifier)
    return encoder.build()


def decode_attach_request(data: bytes) -> AttachRequest:
    decoder = TlvDecoder(data)
    return AttachRequest(
        pid=decoder.get_uint32(FIELD_ATTACH_PID, 0) or 0,
        identifier=decoder.get_string(FIELD_ATTACH_IDENTIFIER, "") or "",
    )


def encode_attach_response(response: AttachResponse) -> bytes:
    encoder = TlvEncoder()
    encoder.put_uint32(FIELD_ATTACH_RESP_SESSION_ID, response.session_id)
    encoder.put_uint32(FIELD_ATTACH_RESP_PID, response.pid)
    if response.process_name:
        encoder.put_string(FIELD_ATTACH_RESP_PROCESS_NAME, response.process_name)
    if response.error.code or response.error.message:
        encoder.put_nested(FIELD_ATTACH_RESP_ERROR, _encode_error(response.error))
    return encoder.build()


def decode_attach_response(data: bytes) -> AttachResponse:
    decoder = TlvDecoder(data)
    try:
        error = _decode_error(decoder.get_nested(FIELD_ATTACH_RESP_ERROR))
    except KeyError:
        error = ErrorInfo()
    return AttachResponse(
        session_id=decoder.get_uint32(FIELD_ATTACH_RESP_SESSION_ID, 0) or 0,
        pid=decoder.get_uint32(FIELD_ATTACH_RESP_PID, 0) or 0,
        process_name=decoder.get_string(FIELD_ATTACH_RESP_PROCESS_NAME, "") or "",
        error=error,
    )


def encode_spawn_response(response: SpawnResponse) -> bytes:
    encoder = TlvEncoder()
    encoder.put_uint32(FIELD_SPAWN_RESPONSE_PID, response.pid)
    if response.error.code or response.error.message:
        encoder.put_nested(FIELD_SPAWN_RESPONSE_ERROR, _encode_error(response.error))
    return encoder.build()


def decode_spawn_response(data: bytes) -> SpawnResponse:
    decoder = TlvDecoder(data)
    try:
        error = _decode_error(decoder.get_nested(FIELD_SPAWN_RESPONSE_ERROR))
    except KeyError:
        error = ErrorInfo()
    return SpawnResponse(pid=decoder.get_uint32(FIELD_SPAWN_RESPONSE_PID, 0) or 0, error=error)


def encode_detach_request(request: DetachRequest) -> bytes:
    encoder = TlvEncoder()
    encoder.put_uint32(FIELD_DETACH_SESSION_ID, request.session_id)
    return encoder.build()


def decode_detach_request(data: bytes) -> DetachRequest:
    decoder = TlvDecoder(data)
    return DetachRequest(session_id=decoder.get_uint32(FIELD_DETACH_SESSION_ID, 0) or 0)


def encode_detach_response(response: DetachResponse) -> bytes:
    encoder = TlvEncoder()
    encoder.put_uint32(FIELD_DETACH_RESP_SESSION_ID, response.session_id)
    if response.error.code or response.error.message:
        encoder.put_nested(FIELD_DETACH_RESP_ERROR, _encode_error(response.error))
    return encoder.build()


def decode_detach_response(data: bytes) -> DetachResponse:
    decoder = TlvDecoder(data)
    try:
        error = _decode_error(decoder.get_nested(FIELD_DETACH_RESP_ERROR))
    except KeyError:
        error = ErrorInfo()
    return DetachResponse(
        session_id=decoder.get_uint32(FIELD_DETACH_RESP_SESSION_ID, 0) or 0,
        error=error,
    )


def encode_resume_request(request: ResumeRequest) -> bytes:
    encoder = TlvEncoder()
    encoder.put_uint32(FIELD_RESUME_PID, request.pid)
    return encoder.build()


def decode_resume_request(data: bytes) -> ResumeRequest:
    decoder = TlvDecoder(data)
    return ResumeRequest(pid=decoder.get_uint32(FIELD_RESUME_PID, 0) or 0)


def encode_resume_response(response: ResumeResponse) -> bytes:
    encoder = TlvEncoder()
    encoder.put_uint32(FIELD_RESUME_RESP_PID, response.pid)
    if response.error.code or response.error.message:
        encoder.put_nested(FIELD_RESUME_RESP_ERROR, _encode_error(response.error))
    return encoder.build()


def decode_resume_response(data: bytes) -> ResumeResponse:
    decoder = TlvDecoder(data)
    try:
        error = _decode_error(decoder.get_nested(FIELD_RESUME_RESP_ERROR))
    except KeyError:
        error = ErrorInfo()
    return ResumeResponse(
        pid=decoder.get_uint32(FIELD_RESUME_RESP_PID, 0) or 0,
        error=error,
    )


def encode_agent_ready(ready: AgentReady) -> bytes:
    encoder = TlvEncoder()
    encoder.put_uint32(FIELD_AGENT_READY_PID, ready.pid)
    encoder.put_string(FIELD_AGENT_READY_PROCESS_NAME, ready.process_name)
    if ready.spawn_token:
        encoder.put_string(FIELD_AGENT_READY_SPAWN_TOKEN, ready.spawn_token)
    encoder.put_string(FIELD_AGENT_READY_ARCH, ready.arch)
    encoder.put_string(FIELD_AGENT_READY_VERSION, ready.version)
    encoder.put_uint8(FIELD_AGENT_READY_STAGE, int(ready.stage))
    return encoder.build()


def decode_agent_ready(data: bytes) -> AgentReady:
    decoder = TlvDecoder(data)
    stage = decoder.get_uint8(FIELD_AGENT_READY_STAGE, int(AgentReadyStage.RUNTIME))
    return AgentReady(
        pid=decoder.get_uint32(FIELD_AGENT_READY_PID, 0) or 0,
        process_name=decoder.get_string(FIELD_AGENT_READY_PROCESS_NAME, "") or "",
        spawn_token=decoder.get_string(FIELD_AGENT_READY_SPAWN_TOKEN, "") or "",
        arch=decoder.get_string(FIELD_AGENT_READY_ARCH, "") or "",
        version=decoder.get_string(FIELD_AGENT_READY_VERSION, "") or "",
        stage=AgentReadyStage(stage),
    )


def encode_script_create(create: ScriptCreate) -> bytes:
    encoder = TlvEncoder()
    encoder.put_uint32(FIELD_SCRIPT_CREATE_SESSION_ID, create.session_id)
    encoder.put_string(FIELD_SCRIPT_CREATE_SOURCE, create.source)
    if create.name:
        encoder.put_string(FIELD_SCRIPT_CREATE_NAME, create.name)
    return encoder.build()


def decode_script_create(data: bytes) -> ScriptCreate:
    decoder = TlvDecoder(data)
    return ScriptCreate(
        session_id=decoder.get_uint32(FIELD_SCRIPT_CREATE_SESSION_ID, 0) or 0,
        source=decoder.get_string(FIELD_SCRIPT_CREATE_SOURCE, "") or "",
        name=decoder.get_string(FIELD_SCRIPT_CREATE_NAME, "") or "",
    )


def encode_script_create_response(response: ScriptCreateResponse) -> bytes:
    encoder = TlvEncoder()
    encoder.put_uint32(FIELD_SCRIPT_CREATE_RESP_SCRIPT_ID, response.script_id)
    encoder.put_uint8(FIELD_SCRIPT_CREATE_RESP_SUCCESS, 1 if response.success else 0)
    if response.error.code or response.error.message:
        encoder.put_nested(FIELD_SCRIPT_CREATE_RESP_ERROR, _encode_error(response.error))
    return encoder.build()


def decode_script_create_response(data: bytes) -> ScriptCreateResponse:
    decoder = TlvDecoder(data)
    try:
        error = _decode_error(decoder.get_nested(FIELD_SCRIPT_CREATE_RESP_ERROR))
    except KeyError:
        error = ErrorInfo()
    return ScriptCreateResponse(
        script_id=decoder.get_uint32(FIELD_SCRIPT_CREATE_RESP_SCRIPT_ID, 0) or 0,
        success=bool(decoder.get_uint8(FIELD_SCRIPT_CREATE_RESP_SUCCESS, 0)),
        error=error,
    )


def encode_script_response(response: ScriptResponse) -> bytes:
    encoder = TlvEncoder()
    encoder.put_uint32(FIELD_SCRIPT_CREATE_RESP_SCRIPT_ID, response.script_id)
    encoder.put_uint8(FIELD_SCRIPT_CREATE_RESP_SUCCESS, 1 if response.success else 0)
    if response.error.code or response.error.message:
        encoder.put_nested(FIELD_SCRIPT_CREATE_RESP_ERROR, _encode_error(response.error))
    return encoder.build()


def decode_script_response(data: bytes) -> ScriptResponse:
    decoder = TlvDecoder(data)
    try:
        error = _decode_error(decoder.get_nested(FIELD_SCRIPT_CREATE_RESP_ERROR))
    except KeyError:
        error = ErrorInfo()
    return ScriptResponse(
        script_id=decoder.get_uint32(FIELD_SCRIPT_CREATE_RESP_SCRIPT_ID, 0) or 0,
        success=bool(decoder.get_uint8(FIELD_SCRIPT_CREATE_RESP_SUCCESS, 0)),
        error=error,
    )


def encode_script_load(load: ScriptLoad) -> bytes:
    encoder = TlvEncoder()
    encoder.put_uint32(FIELD_SCRIPT_LOAD_SCRIPT_ID, load.script_id)
    return encoder.build()


def decode_script_load(data: bytes) -> ScriptLoad:
    decoder = TlvDecoder(data)
    return ScriptLoad(script_id=decoder.get_uint32(FIELD_SCRIPT_LOAD_SCRIPT_ID, 0) or 0)


def encode_script_unload(unload: ScriptUnload) -> bytes:
    encoder = TlvEncoder()
    encoder.put_uint32(FIELD_SCRIPT_UNLOAD_SCRIPT_ID, unload.script_id)
    return encoder.build()


def decode_script_unload(data: bytes) -> ScriptUnload:
    decoder = TlvDecoder(data)
    return ScriptUnload(script_id=decoder.get_uint32(FIELD_SCRIPT_UNLOAD_SCRIPT_ID, 0) or 0)


def encode_script_message(message: ScriptMessage) -> bytes:
    encoder = TlvEncoder()
    encoder.put_uint32(FIELD_SCRIPT_MESSAGE_SCRIPT_ID, message.script_id)
    encoder.put_string(FIELD_SCRIPT_MESSAGE_MESSAGE, message.message)
    if message.data:
        encoder.put_bytes(FIELD_SCRIPT_MESSAGE_DATA, message.data)
    return encoder.build()


def decode_script_message(data: bytes) -> ScriptMessage:
    decoder = TlvDecoder(data)
    return ScriptMessage(
        script_id=decoder.get_uint32(FIELD_SCRIPT_MESSAGE_SCRIPT_ID, 0) or 0,
        message=decoder.get_string(FIELD_SCRIPT_MESSAGE_MESSAGE, "") or "",
        data=decoder.get_bytes(FIELD_SCRIPT_MESSAGE_DATA, b"") or b"",
    )


def encode_script_post(post: ScriptPost) -> bytes:
    encoder = TlvEncoder()
    encoder.put_uint32(FIELD_SCRIPT_MESSAGE_SCRIPT_ID, post.script_id)
    encoder.put_string(FIELD_SCRIPT_MESSAGE_MESSAGE, post.message)
    if post.data:
        encoder.put_bytes(FIELD_SCRIPT_MESSAGE_DATA, post.data)
    return encoder.build()


def decode_script_post(data: bytes) -> ScriptPost:
    decoder = TlvDecoder(data)
    return ScriptPost(
        script_id=decoder.get_uint32(FIELD_SCRIPT_MESSAGE_SCRIPT_ID, 0) or 0,
        message=decoder.get_string(FIELD_SCRIPT_MESSAGE_MESSAGE, "") or "",
        data=decoder.get_bytes(FIELD_SCRIPT_MESSAGE_DATA, b"") or b"",
    )


def encode_rpc_request(request: RpcRequest) -> bytes:
    encoder = TlvEncoder()
    encoder.put_uint32(FIELD_RPC_SCRIPT_ID, request.script_id)
    encoder.put_string(FIELD_RPC_METHOD, request.method)
    if request.args_json:
        encoder.put_string(FIELD_RPC_ARGS_JSON, request.args_json)
    return encoder.build()


def decode_rpc_request(data: bytes) -> RpcRequest:
    decoder = TlvDecoder(data)
    return RpcRequest(
        script_id=decoder.get_uint32(FIELD_RPC_SCRIPT_ID, 0) or 0,
        method=decoder.get_string(FIELD_RPC_METHOD, "") or "",
        args_json=decoder.get_string(FIELD_RPC_ARGS_JSON, "") or "",
    )


def encode_rpc_response(response: RpcResponse) -> bytes:
    encoder = TlvEncoder()
    encoder.put_uint32(FIELD_RPC_SCRIPT_ID, response.script_id)
    encoder.put_uint8(FIELD_RPC_SUCCESS, 1 if response.success else 0)
    if response.result_json:
        encoder.put_string(FIELD_RPC_RESULT_JSON, response.result_json)
    if response.error.code or response.error.message:
        encoder.put_nested(FIELD_RPC_ERROR, _encode_error(response.error))
    return encoder.build()


def decode_rpc_response(data: bytes) -> RpcResponse:
    decoder = TlvDecoder(data)
    try:
        error = _decode_error(decoder.get_nested(FIELD_RPC_ERROR))
    except KeyError:
        error = ErrorInfo()
    return RpcResponse(
        script_id=decoder.get_uint32(FIELD_RPC_SCRIPT_ID, 0) or 0,
        success=bool(decoder.get_uint8(FIELD_RPC_SUCCESS, 0)),
        result_json=decoder.get_string(FIELD_RPC_RESULT_JSON, "") or "",
        error=error,
    )


def encode_process_list_request() -> bytes:
    return TlvEncoder().build()


def encode_process_list_response(response: ProcessListResponse) -> bytes:
    encoder = TlvEncoder()
    for process in response.processes:
        nested = TlvEncoder()
        nested.put_uint32(FIELD_PROCESS_ENTRY_PID, process.pid)
        nested.put_string(FIELD_PROCESS_ENTRY_NAME, process.name)
        encoder.put_nested(FIELD_PROCESS_LIST_ENTRY, nested)
    if response.error.code or response.error.message:
        encoder.put_nested(FIELD_PROCESS_LIST_ERROR, _encode_error(response.error))
    return encoder.build()


def decode_process_list_response(data: bytes) -> ProcessListResponse:
    decoder = TlvDecoder(data)
    processes = [
        ProcessEntry(
            pid=nested.get_uint32(FIELD_PROCESS_ENTRY_PID, 0) or 0,
            name=nested.get_string(FIELD_PROCESS_ENTRY_NAME, "") or "",
        )
        for nested in decoder.get_all_nested(FIELD_PROCESS_LIST_ENTRY)
    ]
    try:
        error = _decode_error(decoder.get_nested(FIELD_PROCESS_LIST_ERROR))
    except KeyError:
        error = ErrorInfo()
    return ProcessListResponse(processes=processes, error=error)


def encode_app_list_request() -> bytes:
    return TlvEncoder().build()


def encode_app_list_response(response: AppListResponse) -> bytes:
    encoder = TlvEncoder()
    for app in response.apps:
        nested = TlvEncoder()
        nested.put_string(FIELD_APP_ENTRY_PACKAGE_NAME, app.package_name)
        encoder.put_nested(FIELD_APP_LIST_ENTRY, nested)
    if response.error.code or response.error.message:
        encoder.put_nested(FIELD_APP_LIST_ERROR, _encode_error(response.error))
    return encoder.build()


def decode_app_list_response(data: bytes) -> AppListResponse:
    decoder = TlvDecoder(data)
    apps = [
        AppEntry(package_name=nested.get_string(FIELD_APP_ENTRY_PACKAGE_NAME, "") or "")
        for nested in decoder.get_all_nested(FIELD_APP_LIST_ENTRY)
    ]
    try:
        error = _decode_error(decoder.get_nested(FIELD_APP_LIST_ERROR))
    except KeyError:
        error = ErrorInfo()
    return AppListResponse(apps=apps, error=error)
