from typing import Optional

from .script import Script


class Session:
    def __init__(
        self,
        device,
        session_id: int,
        pid: int,
        process_name: str,
        default_timeout_ms: int = 5000,
    ) -> None:
        self.device = device
        self.session_id = session_id
        self.pid = pid
        self.process_name = process_name
        self.default_timeout_ms = default_timeout_ms

    def create_script(self, source: str, name: str = "script.js") -> Script:
        return Script(
            session=self,
            source=source,
            name=name,
            default_timeout_ms=self.default_timeout_ms,
        )

    def detach(self, timeout_ms: Optional[int] = None):
        return self.device.detach(self.session_id, timeout_ms=timeout_ms)

