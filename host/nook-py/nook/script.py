from typing import Callable, List, Optional

from .protocol import ScriptMessage


ScriptMessageCallback = Callable[[ScriptMessage, bytes], None]


class Script:
    def __init__(
        self,
        session,
        source: str,
        name: str = "script.js",
        script_id: Optional[int] = None,
        default_timeout_ms: int = 5000,
    ) -> None:
        self.session = session
        self.source = source
        self.name = name
        self.script_id = script_id
        self.default_timeout_ms = default_timeout_ms
        self._message_callbacks: List[ScriptMessageCallback] = []

    def create(self, timeout_ms: Optional[int] = None) -> int:
        response = self.session.device.create_script(
            session_id=self.session.session_id,
            source=self.source,
            name=self.name,
            timeout_ms=timeout_ms or self.default_timeout_ms,
        )
        self.script_id = response.script_id
        for callback in self._message_callbacks:
            self.session.device.add_script_message_callback(self.script_id, callback)
        return self.script_id

    def load(self, timeout_ms: Optional[int] = None):
        self._require_script_id()
        return self.session.device.load_script(
            self.script_id,
            timeout_ms=timeout_ms or self.default_timeout_ms,
        )

    def unload(self, timeout_ms: Optional[int] = None):
        self._require_script_id()
        current_script_id = self.script_id
        try:
            return self.session.device.unload_script(
                current_script_id,
                timeout_ms=timeout_ms or self.default_timeout_ms,
            )
        finally:
            if current_script_id is not None:
                for callback in self._message_callbacks:
                    self.session.device.remove_script_message_callback(current_script_id, callback)
            self.script_id = None

    def post(self, message: str, data: bytes = b"") -> None:
        self._require_script_id()
        self.session.device.post_script_message(self.script_id, message, data)

    def call(self, method: str, *args, timeout_ms: Optional[int] = None):
        self._require_script_id()
        if not method:
            raise ValueError("rpc method is empty")
        return self.session.device.call_rpc(
            self.script_id,
            method,
            list(args),
            timeout_ms=timeout_ms or self.default_timeout_ms,
        )

    def wait_for_message(self, timeout_ms: Optional[int] = None):
        self._require_script_id()
        return self.session.device.wait_for_script_message(
            timeout_ms=timeout_ms or self.default_timeout_ms,
            script_id=self.script_id,
        )

    def on(self, event: str, callback: ScriptMessageCallback) -> None:
        if event != "message":
            raise ValueError("unsupported event: %s" % event)
        if callback not in self._message_callbacks:
            self._message_callbacks.append(callback)
        if self.script_id is not None:
            self.session.device.add_script_message_callback(self.script_id, callback)

    def off(self, event: str, callback: Optional[ScriptMessageCallback] = None) -> None:
        if event != "message":
            raise ValueError("unsupported event: %s" % event)
        if callback is None:
            callbacks = list(self._message_callbacks)
            self._message_callbacks.clear()
            if self.script_id is not None:
                for item in callbacks:
                    self.session.device.remove_script_message_callback(self.script_id, item)
            return

        self._message_callbacks = [item for item in self._message_callbacks if item is not callback]
        if self.script_id is not None:
            self.session.device.remove_script_message_callback(self.script_id, callback)

    def _require_script_id(self) -> None:
        if self.script_id is None:
            raise ValueError("script has not been created yet")
