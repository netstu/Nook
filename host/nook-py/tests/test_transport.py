import os
import sys
import unittest
from unittest import mock


TEST_ROOT = os.path.dirname(__file__)
PACKAGE_ROOT = os.path.abspath(os.path.join(TEST_ROOT, ".."))
if PACKAGE_ROOT not in sys.path:
    sys.path.insert(0, PACKAGE_ROOT)


from nook._transport import TcpConnection  # noqa: E402
from nook.protocol import Frame, MessageType  # noqa: E402


class FakeSocket:
    def __init__(self, chunks) -> None:
        self._chunks = list(chunks)
        self.timeout_values = []

    def settimeout(self, value) -> None:
        self.timeout_values.append(value)

    def recv(self, size: int) -> bytes:
        if not self._chunks:
            raise AssertionError("unexpected recv without queued data")
        chunk = self._chunks.pop(0)
        if len(chunk) > size:
            self._chunks.insert(0, chunk[size:])
            return chunk[:size]
        return chunk


class TcpConnectionTests(unittest.TestCase):
    def _make_connection(self, fake_socket: FakeSocket) -> TcpConnection:
        connection = TcpConnection.__new__(TcpConnection)
        connection._socket = fake_socket
        return connection

    def test_recv_frame_recomputes_timeout_for_partial_reads(self) -> None:
        frame = Frame(MessageType.SCRIPT_LOAD, 7, b"\x11\x22\x33")
        encoded = frame.serialize()
        fake_socket = FakeSocket([encoded[:4], encoded[4:8], encoded[8:11], encoded[11:]])
        connection = self._make_connection(fake_socket)

        with mock.patch(
            "nook._transport.time.monotonic",
            side_effect=[10.0, 10.1, 10.2, 10.3, 10.4, 10.5],
        ):
            decoded = connection.recv_frame(timeout_ms=1000)

        self.assertEqual(decoded.message_type, MessageType.SCRIPT_LOAD)
        self.assertEqual(decoded.message_id, 7)
        self.assertEqual(decoded.payload, b"\x11\x22\x33")
        self.assertEqual(len(fake_socket.timeout_values), 5)
        self.assertGreater(fake_socket.timeout_values[0], fake_socket.timeout_values[-1])

    def test_recv_frame_times_out_when_deadline_expires_mid_read(self) -> None:
        frame = Frame(MessageType.SCRIPT_LOAD, 7, b"\x11\x22\x33")
        encoded = frame.serialize()
        fake_socket = FakeSocket([encoded[:5]])
        connection = self._make_connection(fake_socket)

        with mock.patch("nook._transport.time.monotonic", side_effect=[20.0, 20.2, 21.1]):
            with self.assertRaises(TimeoutError):
                connection.recv_frame(timeout_ms=1000)


if __name__ == "__main__":
    unittest.main()
