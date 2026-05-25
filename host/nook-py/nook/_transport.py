import socket
import subprocess
import time
from typing import Optional

from .protocol import Frame


class TcpConnection:
    def __init__(self, host: str = "127.0.0.1", port: int = 27042, timeout_ms: int = 5000) -> None:
        self._host = host
        self._port = port
        self._default_timeout_ms = timeout_ms
        self._socket = socket.create_connection((host, port), timeout_ms / 1000.0)

    def close(self) -> None:
        try:
            self._socket.close()
        except OSError:
            pass

    def send_frame(self, frame: Frame) -> None:
        self._socket.sendall(frame.serialize())

    def recv_frame(self, timeout_ms: Optional[int] = None) -> Frame:
        deadline = None if timeout_ms is None else time.monotonic() + (timeout_ms / 1000.0)
        header = self._recv_exact(Frame.HEADER_SIZE, deadline=deadline)
        payload_len = int.from_bytes(header[0:4], "big")
        if payload_len > Frame.MAX_PAYLOAD_SIZE:
            raise ValueError("payload too large")
        payload = self._recv_exact(payload_len, deadline=deadline) if payload_len else b""
        frame, _ = Frame.parse(header + payload)
        return frame

    def _recv_exact(self, size: int, deadline: Optional[float] = None) -> bytes:
        chunks = []
        remaining = size
        while remaining > 0:
            self._set_socket_timeout(deadline)
            try:
                chunk = self._socket.recv(remaining)
            except socket.timeout as exc:
                raise TimeoutError("recv frame timed out") from exc
            if not chunk:
                raise ConnectionError("socket closed")
            chunks.append(chunk)
            remaining -= len(chunk)
        return b"".join(chunks)

    def _set_socket_timeout(self, deadline: Optional[float]) -> None:
        if deadline is None:
            self._socket.settimeout(None)
            return
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("recv frame timed out")
        self._socket.settimeout(remaining)


def ensure_adb_forward(local_port: int, remote_port: int, serial: Optional[str] = None) -> None:
    command = ["adb"]
    if serial:
        command.extend(["-s", serial])
    command.extend(["forward", f"tcp:{local_port}", f"tcp:{remote_port}"])
    subprocess.run(command, check=True)
