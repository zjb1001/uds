"""Unit tests for the ISO-TP transport layer (tools/uds/transport.py).

These tests exercise the pure ISO-TP framing logic using mock CAN buses so
that no physical or virtual CAN interface is required.
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any
from unittest.mock import MagicMock

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))

from uds.exceptions import UdsProtocolError, UdsTimeoutError
from uds.transport import (
    _CANFD_CF_DATA_BYTES,
    _FC_CONTINUE,
    IsoTpTransport,
)

# ── Helpers ──────────────────────────────────────────────────────────────────


def _make_transport(fd: bool = False) -> tuple[IsoTpTransport, MagicMock, MagicMock]:
    """Return (transport, mock_bus, mock_msg_cls) ready for use."""
    tp = IsoTpTransport("vcan0", tx_id=0x601, rx_id=0x681, timeout=1.0, fd=fd)
    mock_bus = MagicMock()
    mock_msg_cls = MagicMock(side_effect=lambda **kw: kw)
    tp._bus = mock_bus
    tp._msg_cls = mock_msg_cls
    return tp, mock_bus, mock_msg_cls


def _rx_msg(arb_id: int, data: bytes) -> MagicMock:
    """Create a mock python-can Message received from the bus."""
    msg = MagicMock()
    msg.arbitration_id = arb_id
    msg.data = data
    return msg


# ── Classic CAN tests ─────────────────────────────────────────────────────────


@pytest.mark.unit
class TestClassicCanSingleFrame:
    """ISO-TP Single Frame encode/decode for classic 8-byte CAN."""

    def test_send_sf_basic(self) -> None:
        tp, bus, msg_cls = _make_transport()
        tp.send(b"\x22\xf1\x90")
        bus.send.assert_called_once()
        sent: dict[str, Any] = bus.send.call_args[0][0]
        data = bytes(sent["data"])
        assert data[0] == 0x03  # SF PCI: length 3
        assert data[1:4] == b"\x22\xf1\x90"
        assert sent["is_fd"] is False

    def test_send_sf_max_classic(self) -> None:
        payload = bytes(range(7))
        tp, bus, _ = _make_transport()
        tp.send(payload)
        bus.send.assert_called_once()
        sent = bus.send.call_args[0][0]
        data = bytes(sent["data"])
        assert data[0] == 0x07  # SF PCI: length 7
        assert data[1:8] == payload

    def test_recv_sf_basic(self) -> None:
        tp, bus, _ = _make_transport()
        # Simulate incoming SF: PCI=0x03, then 3 data bytes
        raw = bytes([0x03, 0xAA, 0xBB, 0xCC, 0x00, 0x00, 0x00, 0x00])
        bus.recv.return_value = _rx_msg(0x681, raw)
        result = tp.recv()
        assert result == b"\xaa\xbb\xcc"

    def test_recv_timeout(self) -> None:
        tp, bus, _ = _make_transport()
        bus.recv.return_value = None
        with pytest.raises(UdsTimeoutError):
            tp.recv()

    def test_recv_wrong_rx_id_then_correct(self) -> None:
        """Frames with wrong arbitration ID are silently discarded."""
        tp, bus, _ = _make_transport()
        raw = bytes([0x01, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
        wrong_id_msg = _rx_msg(0x7FF, raw)
        correct_msg = _rx_msg(0x681, raw)
        bus.recv.side_effect = [wrong_id_msg, correct_msg]
        result = tp.recv()
        assert result == b"\xff"

    def test_recv_unexpected_frame_type(self) -> None:
        tp, bus, _ = _make_transport()
        # CF as first frame is not valid
        raw = bytes([0x21, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
        bus.recv.return_value = _rx_msg(0x681, raw)
        with pytest.raises(UdsProtocolError):
            tp.recv()


@pytest.mark.unit
class TestClassicCanMultiFrame:
    """ISO-TP multi-frame send/receive for classic 8-byte CAN."""

    def _make_fc_bytes(self, bs: int = 0, stmin: int = 0) -> bytes:
        return bytes([_FC_CONTINUE, bs, stmin, 0, 0, 0, 0, 0])

    def test_send_multi_ff_then_cf(self) -> None:
        payload = bytes(range(20))
        tp, bus, _ = _make_transport()
        fc_raw = self._make_fc_bytes()
        bus.recv.return_value = _rx_msg(0x681, fc_raw)
        tp.send(payload)
        # FF + 2 CFs: 6 (FF) + 7 (CF1) + 7 (CF2) = 20 bytes total → 3 sends
        assert bus.send.call_count == 3
        ff_data = bytes(bus.send.call_args_list[0][0][0]["data"])
        assert ff_data[0] == 0x10  # FF PCI high nibble
        assert ff_data[1] == 0x14  # length=20

    def test_recv_multi_frame(self) -> None:
        tp, bus, _ = _make_transport()
        # First frame: FF declaring 14 bytes total, first 6 embedded
        ff_raw = bytes([0x10, 0x0E, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06])
        # Consecutive frames
        cf1_raw = bytes([0x21, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D])
        cf2_raw = bytes([0x22, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
        bus.recv.side_effect = [
            _rx_msg(0x681, ff_raw),
            _rx_msg(0x681, cf1_raw),
            _rx_msg(0x681, cf2_raw),
        ]
        result = tp.recv()
        assert result == bytes(range(1, 15))

    def test_recv_multi_seq_error(self) -> None:
        tp, bus, _ = _make_transport()
        ff_raw = bytes([0x10, 0x0E, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06])
        # Wrong SN=2 when we expect 1
        bad_cf = bytes([0x22, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
        bus.recv.side_effect = [_rx_msg(0x681, ff_raw), _rx_msg(0x681, bad_cf)]
        with pytest.raises(UdsProtocolError):
            tp.recv()


# ── CAN FD tests ─────────────────────────────────────────────────────────────


@pytest.mark.unit
class TestCanFdTransportInit:
    """IsoTpTransport FD flag is propagated correctly."""

    def test_fd_flag_default_false(self) -> None:
        tp = IsoTpTransport("vcan0", tx_id=0x601, rx_id=0x681)
        assert tp._fd is False

    def test_fd_flag_true(self) -> None:
        tp = IsoTpTransport("vcan0", tx_id=0x601, rx_id=0x681, fd=True)
        assert tp._fd is True

    def test_make_message_fd_flags(self) -> None:
        tp, _, msg_cls = _make_transport(fd=True)
        tp._make_message(bytearray(3))
        _, kwargs = msg_cls.call_args
        assert kwargs["is_fd"] is True
        assert kwargs["bitrate_switch"] is True

    def test_make_message_classic_flags(self) -> None:
        tp, _, msg_cls = _make_transport(fd=False)
        tp._make_message(bytearray(8))
        _, kwargs = msg_cls.call_args
        assert kwargs["is_fd"] is False
        assert kwargs["bitrate_switch"] is False


@pytest.mark.unit
class TestCanFdSingleFrame:
    """ISO-TP CAN FD Single Frame (SF) logic."""

    def test_send_sf_classic_in_fd_mode(self) -> None:
        """Payloads ≤ 7 bytes use the classic 1-byte PCI even in FD mode."""
        tp, bus, _ = _make_transport(fd=True)
        tp.send(b"\x22\xf1\x90")
        sent = bus.send.call_args[0][0]
        data = bytes(sent["data"])
        assert data[0] == 0x03  # classic SF PCI
        assert data[1:4] == b"\x22\xf1\x90"

    def test_send_sf_escape_8_bytes(self) -> None:
        """8-byte payload triggers CAN FD escape-sequence PCI."""
        payload = bytes(range(8))
        tp, bus, _ = _make_transport(fd=True)
        tp.send(payload)
        sent = bus.send.call_args[0][0]
        data = bytes(sent["data"])
        assert data[0] == 0x00  # escape byte
        assert data[1] == 0x08  # SF_DL = 8
        assert data[2:10] == payload

    def test_send_sf_escape_max(self) -> None:
        """62-byte payload fits in a single CAN FD frame."""
        payload = bytes(range(62))
        tp, bus, _ = _make_transport(fd=True)
        tp.send(payload)
        bus.send.assert_called_once()
        sent = bus.send.call_args[0][0]
        data = bytes(sent["data"])
        assert data[0] == 0x00
        assert data[1] == 62
        assert data[2:64] == payload

    def test_recv_sf_escape_sequence(self) -> None:
        """Receive a CAN FD SF using the escape-sequence PCI."""
        payload = bytes(range(10))
        raw = bytes([0x00, 0x0A]) + payload
        tp, bus, _ = _make_transport(fd=True)
        bus.recv.return_value = _rx_msg(0x681, raw)
        result = tp.recv()
        assert result == payload

    def test_recv_sf_classic_in_fd_mode(self) -> None:
        """Classic SF PCI (length ≤ 7) is still accepted in FD mode."""
        raw = bytes([0x03, 0xAA, 0xBB, 0xCC] + [0x00] * 60)
        tp, bus, _ = _make_transport(fd=True)
        bus.recv.return_value = _rx_msg(0x681, raw)
        result = tp.recv()
        assert result == b"\xaa\xbb\xcc"

    def test_63_byte_payload_triggers_multiframe(self) -> None:
        """63-byte payload (> 62) must use multi-frame, not SF."""
        payload = bytes(range(63))
        tp, bus, _ = _make_transport(fd=True)
        # Provide FC so multi-frame send completes
        fc_raw = bytes([_FC_CONTINUE, 0, 0])
        bus.recv.return_value = _rx_msg(0x681, fc_raw)
        tp.send(payload)
        # First call is FF, remaining are CFs
        assert bus.send.call_count >= 2

    def test_send_threshold_62_still_sf(self) -> None:
        """Exactly 62 bytes should still be a single frame in FD mode."""
        payload = bytes(range(62))
        tp, bus, _ = _make_transport(fd=True)
        tp.send(payload)
        bus.send.assert_called_once()


@pytest.mark.unit
class TestCanFdMultiFrame:
    """ISO-TP CAN FD multi-frame send/receive."""

    def _make_fc_bytes(self, bs: int = 0, stmin: int = 0) -> bytes:
        return bytes([_FC_CONTINUE, bs, stmin])

    def test_send_multi_fd_regular_ff(self) -> None:
        """Multi-frame with regular FF (FF_DL ≤ 4095)."""
        # 100-byte payload → 1 FF + 1 CF
        payload = bytes(range(100))
        tp, bus, _ = _make_transport(fd=True)
        fc_raw = self._make_fc_bytes()
        bus.recv.return_value = _rx_msg(0x681, fc_raw)
        tp.send(payload)
        # FF + 1 CF (62 in FF + 38 in one CF = 100)
        assert bus.send.call_count == 2
        ff_data = bytes(bus.send.call_args_list[0][0][0]["data"])
        assert ff_data[0] == 0x10  # FF PCI
        assert ff_data[1] == 0x64  # 100 & 0xFF

    def test_send_multi_fd_cf_chunk_size(self) -> None:
        """CFs carry up to 63 bytes each in FD mode."""
        # 200 bytes: FF (62 bytes) + 3 CFs (63 + 63 + 12 = 138)
        payload = bytes(range(200))
        tp, bus, _ = _make_transport(fd=True)
        fc_raw = self._make_fc_bytes()
        bus.recv.return_value = _rx_msg(0x681, fc_raw)
        tp.send(payload)
        # FF + ceil((200-62)/63) = FF + 3 CFs
        assert bus.send.call_count == 4
        # Verify second send (first CF) carries 63 bytes
        cf1_data = bytes(bus.send.call_args_list[1][0][0]["data"])
        assert len(cf1_data) == 64  # 1 PCI + 63 data

    def test_send_multi_fd_extended_ff(self) -> None:
        """Multi-frame with extended FF (FF_DL > 4095)."""
        payload = bytes(5000)
        tp, bus, _ = _make_transport(fd=True)
        fc_raw = self._make_fc_bytes()
        bus.recv.return_value = _rx_msg(0x681, fc_raw)
        tp.send(payload)
        ff_data = bytes(bus.send.call_args_list[0][0][0]["data"])
        # Extended FF PCI: data[0]=0x10, data[1]=0x00
        assert ff_data[0] == 0x10
        assert ff_data[1] == 0x00
        # 32-bit length 5000 = 0x00001388 big-endian
        assert ff_data[2] == 0x00
        assert ff_data[3] == 0x00
        assert ff_data[4] == 0x13
        assert ff_data[5] == 0x88

    def test_recv_multi_fd_regular_ff(self) -> None:
        """Receive a CAN FD multi-frame message (regular FF)."""
        tp, bus, _ = _make_transport(fd=True)
        # 70-byte payload: FF (62 bytes) + CF (8 bytes)
        total = 70
        ff_payload = bytes(range(62))
        cf_payload = bytes(range(62, 70))
        # Regular FF PCI: 0x10, 0x46 (70 = 0x46)
        ff_raw = bytes([0x10, 0x46]) + ff_payload
        cf_raw = bytes([0x21]) + cf_payload
        bus.recv.side_effect = [_rx_msg(0x681, ff_raw), _rx_msg(0x681, cf_raw)]
        result = tp.recv()
        assert len(result) == total
        assert result == bytes(range(62)) + bytes(range(62, 70))

    def test_recv_multi_fd_extended_ff(self) -> None:
        """Receive a CAN FD multi-frame message with extended FF (FF_DL > 4095)."""
        tp, bus, _ = _make_transport(fd=True)
        total = 5000
        # Extended FF: data[0]=0x10, data[1]=0x00, data[2..5]=5000 big-endian
        # 58 bytes embedded
        ff_payload = bytes(58)
        ff_raw = bytes([0x10, 0x00, 0x00, 0x00, 0x13, 0x88]) + ff_payload

        # 63-byte CFs for the rest
        remaining = total - 58  # 4942 bytes
        cfs = []
        sn = 1
        offset = 0
        chunk_data = bytes(range(256)) * 20  # enough data
        while offset < remaining:
            chunk = chunk_data[offset : offset + _CANFD_CF_DATA_BYTES]
            cf_raw = bytes([0x20 | (sn & 0x0F)]) + chunk
            cfs.append(_rx_msg(0x681, cf_raw))
            offset += len(chunk)
            sn = (sn + 1) & 0x0F

        bus.recv.side_effect = [_rx_msg(0x681, ff_raw)] + cfs
        result = tp.recv()
        assert len(result) == total

    def test_recv_multi_fd_seq_error(self) -> None:
        """Wrong SN in CF raises UdsProtocolError in FD mode."""
        tp, bus, _ = _make_transport(fd=True)
        ff_raw = bytes([0x10, 0x46]) + bytes(62)
        bad_cf = bytes([0x22]) + bytes(8)  # SN=2 instead of expected 1
        bus.recv.side_effect = [_rx_msg(0x681, ff_raw), _rx_msg(0x681, bad_cf)]
        with pytest.raises(UdsProtocolError):
            tp.recv()
