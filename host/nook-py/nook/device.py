import threading
import time
from collections import defaultdict
import json
import socket
from typing import Callable, DefaultDict, Dict, List, Optional, Tuple

from .errors import NookError
from .protocol import (
    AgentReady,
    AgentReadyStage,
    AppEntry,
    AttachRequest,
    AttachResponse,
    DetachRequest,
    DetachResponse,
    Frame,
    MessageType,
    ProcessEntry,
    ResumeRequest,
    ResumeResponse,
    RpcRequest,
    RpcResponse,
    ScriptCreate,
    ScriptCreateResponse,
    ScriptLoad,
    ScriptMessage,
    ScriptPost,
    ScriptResponse,
    ScriptUnload,
    SpawnRequest,
    SpawnResponse,
    decode_agent_ready,
    decode_app_list_response,
    decode_attach_response,
    decode_detach_response,
    decode_process_list_response,
    decode_rpc_response,
    decode_resume_response,
    decode_script_create_response,
    decode_script_message,
    decode_script_response,
    decode_spawn_response,
    encode_app_list_request,
    encode_attach_request,
    encode_detach_request,
    encode_process_list_request,
    encode_rpc_request,
    encode_resume_request,
    encode_script_create,
    encode_script_load,
    encode_script_post,
    encode_script_unload,
    encode_spawn_request,
)
from .session import Session


class Device:
    _DEFAULT_SPAWN_RESPONSE_TIMEOUT_MS = 20000

    class _SequencedAgentReady:
        __slots__ = ("ready", "sequence")

        def __init__(self, ready: AgentReady, sequence: int) -> None:
            self.ready = ready
            self.sequence = sequence

    def __init__(self, connection, default_timeout_ms: int = 5000) -> None:
        self._connection = connection
        self._default_timeout_ms = default_timeout_ms
        self._send_lock = threading.Lock()
        self._state_lock = threading.Lock()
        self._state_cv = threading.Condition(self._state_lock)
        self._next_message_id = 1
        self._next_receive_sequence = 1
        self._agent_ready_events: List["Device._SequencedAgentReady"] = []
        self._script_messages: List[ScriptMessage] = []
        self._response_frames: DefaultDict[Tuple[MessageType, int], List[Tuple[Frame, int]]] = defaultdict(list)
        self._script_message_callbacks: DefaultDict[int, List[Callable[[ScriptMessage, bytes], None]]] = defaultdict(list)
        self._reader_error: Optional[BaseException] = None
        self._closed = False
        self._reader_thread = threading.Thread(
            target=self._reader_loop,
            name="NookPyReader",
            daemon=True,
        )
        self._reader_thread.start()

    def close(self) -> None:
        with self._state_cv:
            self._closed = True
            self._state_cv.notify_all()
        close = getattr(self._connection, "close", None)
        if callable(close):
            close()
        if self._reader_thread.is_alive():
            self._reader_thread.join(timeout=0.5)

    def enumerate_apps(self, timeout_ms: Optional[int] = None) -> List[AppEntry]:
        frame = self._request_response(
            MessageType.APP_LIST_REQ,
            encode_app_list_request(),
            MessageType.APP_LIST_RESP,
            timeout_ms,
        )
        response = decode_app_list_response(frame.payload)
        self._raise_if_error(response.error, "app list failed")
        return response.apps

    def enumerate_processes(self, timeout_ms: Optional[int] = None) -> List[ProcessEntry]:
        frame = self._request_response(
            MessageType.PROCESS_LIST_REQ,
            encode_process_list_request(),
            MessageType.PROCESS_LIST_RESP,
            timeout_ms,
        )
        response = decode_process_list_response(frame.payload)
        self._raise_if_error(response.error, "process list failed")
        return response.processes

    def spawn(
        self,
        identifier: str,
        argv: Optional[List[str]] = None,
        response_timeout_ms: Optional[int] = None,
        agent_ready_timeout_ms: Optional[int] = None,
    ) -> Session:
        with self._state_cv:
            self._agent_ready_events.clear()
            self._script_messages.clear()
        resolved_agent_ready_timeout = self._resolve_timeout(agent_ready_timeout_ms)
        effective_response_timeout = response_timeout_ms
        if effective_response_timeout is None:
            effective_response_timeout = max(
                self._default_timeout_ms,
                resolved_agent_ready_timeout,
                self._DEFAULT_SPAWN_RESPONSE_TIMEOUT_MS,
            )
        frame, response_sequence = self._request_response(
            MessageType.SPAWN_REQUEST,
            encode_spawn_request(SpawnRequest(identifier=identifier, argv=argv or [])),
            MessageType.SPAWN_RESPONSE,
            effective_response_timeout,
            timeout_error_message="wait spawn response timed out",
            include_sequence=True,
        )
        response = decode_spawn_response(frame.payload)
        self._raise_if_error(response.error, "spawn failed")

        ready = self._wait_for_agent_ready(
            response.pid,
            identifier,
            response_sequence + 1,
            resolved_agent_ready_timeout,
            timeout_error_message="wait runtime agent ready timed out",
        )
        return Session(
            device=self,
            session_id=0,
            pid=response.pid,
            process_name=ready.process_name or identifier,
            default_timeout_ms=self._default_timeout_ms,
        )

    def attach(
        self,
        target,
        timeout_ms: Optional[int] = None,
    ) -> Session:
        with self._state_cv:
            self._agent_ready_events.clear()
        if isinstance(target, int):
            request = AttachRequest(pid=target)
        else:
            request = AttachRequest(identifier=str(target))
        frame, response_sequence = self._request_response(
            MessageType.ATTACH_REQUEST,
            encode_attach_request(request),
            MessageType.ATTACH_RESPONSE,
            timeout_ms,
            include_sequence=True,
        )
        response = decode_attach_response(frame.payload)
        self._raise_if_error(response.error, "attach failed")
        self._wait_for_agent_ready(
            response.pid,
            response.process_name or (str(target) if not isinstance(target, int) else ""),
            response_sequence + 1,
            timeout_ms,
            timeout_error_message="wait runtime agent ready timed out",
        )
        return Session(
            device=self,
            session_id=response.session_id,
            pid=response.pid,
            process_name=response.process_name,
            default_timeout_ms=self._default_timeout_ms,
        )

    def detach(self, session_id: int, timeout_ms: Optional[int] = None) -> DetachResponse:
        frame = self._request_response(
            MessageType.DETACH_REQUEST,
            encode_detach_request(DetachRequest(session_id=session_id)),
            MessageType.DETACH_RESPONSE,
            timeout_ms,
        )
        response = decode_detach_response(frame.payload)
        self._raise_if_error(response.error, "detach failed")
        return response

    def resume(self, pid: int, timeout_ms: Optional[int] = None) -> ResumeResponse:
        frame = self._request_response(
            MessageType.RESUME_REQUEST,
            encode_resume_request(ResumeRequest(pid=pid)),
            MessageType.RESUME_RESPONSE,
            timeout_ms,
        )
        response = decode_resume_response(frame.payload)
        self._raise_if_error(response.error, "resume failed")
        return response

    def create_script(
        self,
        session_id: int,
        source: str,
        name: str,
        timeout_ms: Optional[int] = None,
    ) -> ScriptCreateResponse:
        frame = self._request_response(
            MessageType.SCRIPT_CREATE,
            encode_script_create(ScriptCreate(session_id=session_id, source=source, name=name)),
            MessageType.SCRIPT_CREATE_RESP,
            timeout_ms,
        )
        response = decode_script_create_response(frame.payload)
        if not response.success:
            self._raise_if_error(response.error, "script create callback failed")
            raise NookError("script create callback failed")
        return response

    def load_script(self, script_id: int, timeout_ms: Optional[int] = None) -> ScriptResponse:
        frame = self._request_response(
            MessageType.SCRIPT_LOAD,
            encode_script_load(ScriptLoad(script_id=script_id)),
            MessageType.SCRIPT_LOAD_RESP,
            timeout_ms,
        )
        response = decode_script_response(frame.payload)
        if not response.success:
            self._raise_if_error(response.error, "script load callback failed")
            raise NookError("script load callback failed")
        return response

    def unload_script(self, script_id: int, timeout_ms: Optional[int] = None) -> ScriptResponse:
        frame = self._request_response(
            MessageType.SCRIPT_UNLOAD,
            encode_script_unload(ScriptUnload(script_id=script_id)),
            MessageType.SCRIPT_UNLOAD_RESP,
            timeout_ms,
        )
        response = decode_script_response(frame.payload)
        if not response.success:
            self._raise_if_error(response.error, "script unload callback failed")
            raise NookError("script unload callback failed")
        return response

    def call_rpc(
        self,
        script_id: int,
        method: str,
        args,
        timeout_ms: Optional[int] = None,
    ):
        frame = self._request_response(
            MessageType.RPC_REQUEST,
            encode_rpc_request(
                RpcRequest(
                    script_id=script_id,
                    method=method,
                    args_json=json.dumps(args, ensure_ascii=False),
                )
            ),
            MessageType.RPC_RESPONSE,
            timeout_ms,
        )
        response = decode_rpc_response(frame.payload)
        if not response.success:
            self._raise_if_error(response.error, "rpc call failed")
            raise NookError("rpc call failed")
        try:
            return json.loads(response.result_json or "null")
        except json.JSONDecodeError as exc:
            raise NookError("invalid rpc result json") from exc

    def post_script_message(
        self,
        script_id: int,
        message: str,
        data: bytes = b"",
    ) -> None:
        request_id = self._allocate_message_id()
        frame = Frame(
            MessageType.SCRIPT_POST,
            request_id,
            encode_script_post(ScriptPost(script_id=script_id, message=message, data=data)),
        )
        with self._send_lock:
            self._connection.send_frame(frame)

    def wait_for_script_message(
        self,
        timeout_ms: Optional[int] = None,
        script_id: Optional[int] = None,
    ) -> ScriptMessage:
        timeout = self._resolve_timeout(timeout_ms)
        deadline = time.monotonic() + timeout / 1000.0
        with self._state_cv:
            while True:
                message = self._take_script_message(script_id)
                if message is not None:
                    return message
                self._raise_reader_error_locked()
                self._wait_until_deadline_locked(deadline)

    def add_script_message_callback(
        self,
        script_id: int,
        callback: Callable[[ScriptMessage, bytes], None],
    ) -> None:
        with self._state_cv:
            self._script_message_callbacks[script_id].append(callback)

    def remove_script_message_callback(
        self,
        script_id: int,
        callback: Optional[Callable[[ScriptMessage, bytes], None]] = None,
    ) -> None:
        with self._state_cv:
            callbacks = self._script_message_callbacks.get(script_id)
            if not callbacks:
                return
            if callback is None:
                callbacks.clear()
            else:
                self._script_message_callbacks[script_id] = [
                    item for item in callbacks if item is not callback
                ]
            if not self._script_message_callbacks.get(script_id):
                self._script_message_callbacks.pop(script_id, None)

    def _request_response(
        self,
        request_type: MessageType,
        payload: bytes,
        response_type: MessageType,
        timeout_ms: Optional[int],
        timeout_error_message: str = "operation timed out",
        include_sequence: bool = False,
    ):
        timeout = self._resolve_timeout(timeout_ms)
        deadline = time.monotonic() + timeout / 1000.0
        request_id = self._allocate_message_id()
        request_frame = Frame(request_type, request_id, payload)
        with self._send_lock:
            self._connection.send_frame(request_frame)
        with self._state_cv:
            while True:
                matched = self._take_response_frame_locked(response_type, request_id)
                if matched is not None:
                    frame, sequence = matched
                    if include_sequence:
                        return frame, sequence
                    return frame
                self._raise_reader_error_locked()
                self._wait_until_deadline_locked(deadline, timeout_error_message=timeout_error_message)

    def _wait_for_agent_ready(
        self,
        pid: int,
        process_name: str,
        min_sequence: int,
        timeout_ms: Optional[int],
        timeout_error_message: str = "operation timed out",
    ) -> AgentReady:
        timeout = self._resolve_timeout(timeout_ms)
        deadline = time.monotonic() + timeout / 1000.0
        with self._state_cv:
            while True:
                ready = self._take_agent_ready(pid, process_name, min_sequence)
                if ready is not None:
                    return ready
                self._raise_reader_error_locked()
                self._wait_until_deadline_locked(deadline, timeout_error_message=timeout_error_message)

    def _handle_incoming_frame(self, frame: Frame) -> None:
        callbacks = []
        if frame.message_type == MessageType.AGENT_READY:
            ready = decode_agent_ready(frame.payload)
            with self._state_cv:
                sequence = self._next_receive_sequence
                self._next_receive_sequence += 1
                self._agent_ready_events.append(self._SequencedAgentReady(ready, sequence))
                self._state_cv.notify_all()
            return
        if frame.message_type == MessageType.SCRIPT_MESSAGE:
            message = decode_script_message(frame.payload)
            with self._state_cv:
                self._next_receive_sequence += 1
                self._script_messages.append(message)
                callbacks = list(self._collect_script_callbacks_locked(message))
                self._state_cv.notify_all()
            for callback in callbacks:
                callback(message, message.data)
            return
        with self._state_cv:
            sequence = self._next_receive_sequence
            self._next_receive_sequence += 1
            self._response_frames[(frame.message_type, frame.message_id)].append((frame, sequence))
            self._state_cv.notify_all()

    def _reader_loop(self) -> None:
        while True:
            with self._state_lock:
                if self._closed:
                    return
            try:
                frame = self._connection.recv_frame(timeout_ms=None)
            except (TimeoutError, socket.timeout):
                continue
            except BaseException as exc:
                with self._state_cv:
                    if self._closed:
                        return
                    self._reader_error = exc
                    self._state_cv.notify_all()
                return
            self._handle_incoming_frame(frame)

    def _collect_script_callbacks_locked(
        self,
        message: ScriptMessage,
    ) -> List[Callable[[ScriptMessage, bytes], None]]:
        callbacks: List[Callable[[ScriptMessage, bytes], None]] = []
        if message.script_id == 0:
            for items in self._script_message_callbacks.values():
                callbacks.extend(items)
            return callbacks
        callbacks.extend(self._script_message_callbacks.get(0, []))
        callbacks.extend(self._script_message_callbacks.get(message.script_id, []))
        return callbacks

    def _take_agent_ready(self, pid: int, process_name: str, min_sequence: int) -> Optional[AgentReady]:
        for index, item in enumerate(self._agent_ready_events):
            ready = item.ready
            if item.sequence < min_sequence:
                continue
            if (pid != 0 and ready.pid != pid) or ready.stage != AgentReadyStage.RUNTIME:
                continue
            if process_name and ready.process_name and ready.process_name != process_name:
                continue
            self._agent_ready_events.pop(index)
            return ready
        return None

    def _take_script_message(self, script_id: Optional[int]) -> Optional[ScriptMessage]:
        for index, message in enumerate(self._script_messages):
            if script_id is None or message.script_id in (0, script_id):
                return self._script_messages.pop(index)
        return None

    def _take_response_frame_locked(
        self,
        message_type: MessageType,
        message_id: int,
    ) -> Optional[Tuple[Frame, int]]:
        key = (message_type, message_id)
        frames = self._response_frames.get(key)
        if not frames:
            return None
        frame = frames.pop(0)
        if not frames:
            self._response_frames.pop(key, None)
        return frame

    def _allocate_message_id(self) -> int:
        with self._state_lock:
            message_id = self._next_message_id
            self._next_message_id += 1
            return message_id

    def _wait_until_deadline_locked(
        self,
        deadline: float,
        timeout_error_message: str = "operation timed out",
    ) -> None:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(timeout_error_message)
        self._state_cv.wait(timeout=remaining)

    def _raise_reader_error_locked(self) -> None:
        if self._reader_error is None:
            return
        raise NookError(str(self._reader_error)) from self._reader_error

    def _resolve_timeout(self, timeout_ms: Optional[int]) -> int:
        return self._default_timeout_ms if timeout_ms is None else timeout_ms

    @staticmethod
    def _raise_if_error(error, default_message: str) -> None:
        if getattr(error, "code", 0) != 0:
            raise NookError(error.message or default_message)
