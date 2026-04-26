"""ISO-TP (ISO 15765-2) transport layer over python-can."""

from __future__ import annotations

import contextlib
import time
from typing import Protocol, runtime_checkable

from .exceptions import UdsProtocolError, UdsTimeoutError


@runtime_checkable
class _CanBus(Protocol):
    """Minimal interface expected from a python-can Bus object."""

    def send(self, msg: object) -> None: ...
    def recv(self, timeout: float | None = None) -> object | None: ...
    def shutdown(self) -> None: ...


# ISO-TP frame type nibbles
_SF = 0x0  # Single Frame
_FF = 0x1  # First Frame
_CF = 0x2  # Consecutive Frame
_FC = 0x3  # Flow Control

# Flow control flags
_FC_CONTINUE = 0x30
_FC_WAIT = 0x31
_FC_OVERFLOW = 0x32

_MAX_SF_PAYLOAD = 7
_MAX_CAN_PAYLOAD = 8


class IsoTpTransport:
    """ISO-TP transport over a python-can bus.

    Parameters
    ----------
    channel:
        CAN interface name (e.g. ``"vcan0"``).
    tx_id:
        CAN arbitration ID used for transmitted frames.
    rx_id:
        CAN arbitration ID expected for received frames.
    timeout:
        Receive timeout in seconds.
    """

    def __init__(
        self,
        channel: str,
        tx_id: int,
        rx_id: int,
        timeout: float = 2.0,
    ) -> None:
        self._channel = channel
        self._tx_id = tx_id
        self._rx_id = rx_id
        self._timeout = timeout
        self._bus: _CanBus | None = None
        self._msg_cls: type | None = None  # cached can.Message class

    # ── lifecycle ─────────────────────────────────────────────────────────

    def open(self) -> None:
        """Open the CAN bus interface.

        Raises
        ------
        ImportError
            If ``python-can`` is not installed.
        OSError
            If the interface cannot be opened.
        """
        try:
            import can  # noqa: PLC0415
        except ImportError as exc:
            raise ImportError("python-can is required: pip install python-can") from exc

        try:
            self._bus = can.Bus(channel=self._channel, interface="socketcan")
        except Exception as exc:
            raise OSError(f"Cannot open CAN interface '{self._channel}': {exc}") from exc
        self._msg_cls = can.Message

    def close(self) -> None:
        """Close the CAN bus interface."""
        if self._bus is not None:
            with contextlib.suppress(Exception):
                self._bus.shutdown()
            self._bus = None

    # ── context manager ───────────────────────────────────────────────────

    def __enter__(self) -> IsoTpTransport:
        self.open()
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    # ── public API ────────────────────────────────────────────────────────

    def send(self, data: bytes) -> None:
        """Send *data* using ISO-TP framing.

        Uses a Single Frame for payloads ≤7 bytes; First Frame + Consecutive
        Frames (with Flow Control handshake) for longer payloads.
        """
        if self._bus is None:
            raise OSError("Transport is not open")

        if len(data) <= _MAX_SF_PAYLOAD:
            self._send_sf(data)
        else:
            self._send_multi(data)

    def recv(self) -> bytes:
        """Receive one complete ISO-TP PDU.

        Returns
        -------
        bytes
            The reassembled payload.

        Raises
        ------
        UdsTimeoutError
            If no complete PDU is received within the configured timeout.
        UdsProtocolError
            If an unexpected or malformed frame is received.
        """
        if self._bus is None:
            raise OSError("Transport is not open")

        deadline = time.monotonic() + self._timeout
        frame = self._recv_frame(deadline)
        if frame is None:
            raise UdsTimeoutError("Timeout waiting for ISO-TP frame")

        frame_type = (frame[0] >> 4) & 0x0F

        if frame_type == _SF:
            length = frame[0] & 0x0F
            return bytes(frame[1 : 1 + length])

        if frame_type == _FF:
            return self._recv_multi(frame, deadline)

        raise UdsProtocolError(f"Unexpected frame type 0x{frame_type:X} as first frame")

    # ── private helpers ───────────────────────────────────────────────────

    def _make_message(self, data: bytearray) -> object:
        """Create a python-can Message for transmission."""
        return self._msg_cls(arbitration_id=self._tx_id, data=data, is_extended_id=False)

    def _check_fc_continue(self, fc: bytes | None, context: str) -> tuple[int, int]:
        """Validate a Flow Control frame and return (block_size, st_min_ms).

        Raises UdsTimeoutError if *fc* is None, UdsProtocolError if not CTS.
        """
        if fc is None:
            raise UdsTimeoutError(f"Timeout waiting for Flow Control ({context})")
        if fc[0] != _FC_CONTINUE:
            raise UdsProtocolError(f"Expected FC ContinueToSend, got 0x{fc[0]:02X}")
        return fc[1], fc[2]

    def _send_sf(self, data: bytes) -> None:
        """Send a Single Frame."""
        payload = bytearray(8)
        payload[0] = (_SF << 4) | len(data)
        payload[1 : 1 + len(data)] = data
        self._bus.send(self._make_message(payload))

    def _send_multi(self, data: bytes) -> None:
        """Send a multi-frame ISO-TP message (FF + CFs)."""
        total = len(data)

        # First Frame
        ff = bytearray(8)
        ff[0] = (_FF << 4) | ((total >> 8) & 0x0F)
        ff[1] = total & 0xFF
        ff[2:8] = data[0:6]
        self._bus.send(self._make_message(ff))

        # Wait for Flow Control
        deadline = time.monotonic() + self._timeout
        fc = self._recv_frame(deadline)
        block_size, st_min_ms = self._check_fc_continue(fc, "initial")

        # Consecutive Frames
        sn = 1
        offset = 6
        block_count = 0

        while offset < total:
            chunk = data[offset : offset + 7]
            cf = bytearray(8)
            cf[0] = (_CF << 4) | (sn & 0x0F)
            cf[1 : 1 + len(chunk)] = chunk
            self._bus.send(self._make_message(cf))

            offset += len(chunk)
            sn = (sn + 1) & 0x0F
            block_count += 1

            if st_min_ms:
                time.sleep(st_min_ms / 1000.0)

            if block_size and block_count >= block_size:
                # Wait for next FC
                deadline = time.monotonic() + self._timeout
                fc = self._recv_frame(deadline)
                block_size, st_min_ms = self._check_fc_continue(fc, "block")
                block_count = 0

    def _recv_multi(self, first_frame: bytes, deadline: float) -> bytes:
        """Reassemble a multi-frame message after receiving the First Frame."""
        total = ((first_frame[0] & 0x0F) << 8) | first_frame[1]
        buf = bytearray(first_frame[2:8])

        # Send Flow Control — ContinueToSend, no block limit, no ST min
        fc_frame = bytearray([_FC_CONTINUE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
        self._bus.send(self._make_message(fc_frame))

        expected_sn = 1
        while len(buf) < total:
            frame = self._recv_frame(deadline)
            if frame is None:
                raise UdsTimeoutError("Timeout during multi-frame reception")
            frame_type = (frame[0] >> 4) & 0x0F
            if frame_type != _CF:
                raise UdsProtocolError(f"Expected CF, got frame type 0x{frame_type:X}")
            sn = frame[0] & 0x0F
            if sn != expected_sn:
                raise UdsProtocolError(f"CF sequence error: expected {expected_sn}, got {sn}")
            remaining = total - len(buf)
            buf += frame[1 : 1 + min(7, remaining)]
            expected_sn = (expected_sn + 1) & 0x0F

        return bytes(buf[:total])

    def _recv_frame(self, deadline: float) -> bytes | None:
        """Receive one CAN frame with the expected Rx ID before *deadline*."""
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            msg = self._bus.recv(timeout=min(remaining, 0.1))
            if msg is None:
                continue
            if msg.arbitration_id == self._rx_id:
                return bytes(msg.data)
        return None
