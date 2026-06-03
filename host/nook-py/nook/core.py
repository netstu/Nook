from typing import Optional

from ._transport import TcpConnection, ensure_adb_forward
from .device import Device


def get_device(
    host: str = "127.0.0.1",
    port: int = 27042,
    timeout_ms: int = 5000,
) -> Device:
    return Device(TcpConnection(host=host, port=port, timeout_ms=timeout_ms), default_timeout_ms=timeout_ms)


def get_usb_device(
    local_port: int = 27042,
    remote_port: int = 27042,
    timeout_ms: int = 5000,
    serial: Optional[str] = None,
    remote_abstract: str = "",
) -> Device:
    ensure_adb_forward(
        local_port=local_port,
        remote_port=remote_port,
        serial=serial,
        remote_abstract=remote_abstract,
    )
    return get_device(host="127.0.0.1", port=local_port, timeout_ms=timeout_ms)
