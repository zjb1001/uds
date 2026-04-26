"""Unit tests for UDS data service message encoding/decoding."""

from __future__ import annotations

import sys
from pathlib import Path
from unittest.mock import MagicMock

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))

from uds.data import UdsDidService


def _did_service(response: bytes | None = None) -> UdsDidService:
    """Return a UdsDidService with a mock client."""
    mock_client = MagicMock()
    if response is not None:
        mock_client._request.return_value = response
    return UdsDidService(mock_client)


# ── Read DID request encoding ───────────────────────────────────────────────


@pytest.mark.unit
class TestReadDidEncoding:
    """Service 0x22 request byte construction."""

    def test_single_did_request_bytes(self) -> None:
        """Read DID 0xF190: request = [0x22, 0xF1, 0x90]."""
        mock_client = MagicMock()
        mock_client._request.return_value = bytes([0x62, 0xF1, 0x90, 0x01, 0x02])
        svc = UdsDidService(mock_client)
        svc.read(0xF190)
        call_bytes = mock_client._request.call_args[0][0]
        assert call_bytes[0] == 0x22
        assert call_bytes[1] == 0xF1
        assert call_bytes[2] == 0x90

    def test_single_did_request_length(self) -> None:
        mock_client = MagicMock()
        mock_client._request.return_value = bytes([0x62, 0x00, 0x01, 0xAA])
        svc = UdsDidService(mock_client)
        svc.read(0x0001)
        call_bytes = mock_client._request.call_args[0][0]
        assert len(call_bytes) == 3  # SID + 2-byte DID

    def test_multi_did_request_bytes(self) -> None:
        """Read DID [0xF190, 0xF18C]: request = [0x22, 0xF1, 0x90, 0xF1, 0x8C]."""
        mock_client = MagicMock()
        mock_client._request.return_value = bytes(
            [0x62, 0xF1, 0x90, 0xAA, 0xBB, 0xF1, 0x8C, 0xCC, 0xDD]
        )
        svc = UdsDidService(mock_client)
        svc.read([0xF190, 0xF18C])
        call_bytes = mock_client._request.call_args[0][0]
        assert call_bytes == bytes([0x22, 0xF1, 0x90, 0xF1, 0x8C])


# ── Write DID request encoding ─────────────────────────────────────────────


@pytest.mark.unit
class TestWriteDidEncoding:
    """Service 0x2E request byte construction."""

    def test_write_request_bytes(self) -> None:
        """Write DID 0x0100, data=0xDEAD: request = [0x2E, 0x01, 0x00, 0xDE, 0xAD]."""
        mock_client = MagicMock()
        mock_client._request.return_value = bytes([0x6E, 0x01, 0x00])
        svc = UdsDidService(mock_client)
        svc.write(0x0100, b"\xde\xad")
        call_bytes = mock_client._request.call_args[0][0]
        assert call_bytes == bytes([0x2E, 0x01, 0x00, 0xDE, 0xAD])

    def test_write_request_sid(self) -> None:
        mock_client = MagicMock()
        mock_client._request.return_value = bytes([0x6E, 0xF1, 0x90])
        svc = UdsDidService(mock_client)
        svc.write(0xF190, b"\x01")
        assert mock_client._request.call_args[0][0][0] == 0x2E


# ── ECU Reset request encoding ─────────────────────────────────────────────


@pytest.mark.unit
class TestEcuResetEncoding:
    """Service 0x11 request byte construction."""

    def test_hard_reset_bytes(self) -> None:
        """Hard reset: request = [0x11, 0x01]."""
        mock_client = MagicMock()
        mock_client._request.return_value = bytes([0x51, 0x01])
        svc = UdsDidService(mock_client)
        svc.ecu_reset(1)
        call_bytes = mock_client._request.call_args[0][0]
        assert call_bytes == bytes([0x11, 0x01])

    def test_soft_reset_bytes(self) -> None:
        mock_client = MagicMock()
        mock_client._request.return_value = bytes([0x51, 0x03])
        svc = UdsDidService(mock_client)
        svc.ecu_reset(3)
        assert mock_client._request.call_args[0][0] == bytes([0x11, 0x03])


# ── Comm Control request encoding ─────────────────────────────────────────


@pytest.mark.unit
class TestCommControlEncoding:
    """Service 0x28 request byte construction."""

    def test_comm_control_bytes(self) -> None:
        """comm_control(0x01, 0x01): request = [0x28, 0x01, 0x01]."""
        mock_client = MagicMock()
        mock_client._request.return_value = bytes([0x68, 0x01])
        svc = UdsDidService(mock_client)
        svc.comm_control(0x01, 0x01)
        call_bytes = mock_client._request.call_args[0][0]
        assert call_bytes == bytes([0x28, 0x01, 0x01])

    def test_comm_control_sid(self) -> None:
        mock_client = MagicMock()
        mock_client._request.return_value = bytes([0x68, 0x00])
        svc = UdsDidService(mock_client)
        svc.comm_control(0x00, 0x03)
        assert mock_client._request.call_args[0][0][0] == 0x28


# ── Read DID response parsing ──────────────────────────────────────────────


@pytest.mark.unit
class TestReadDidResponseParsing:
    """Service 0x62 response parsing → {did_id: data}."""

    def test_single_did_response(self) -> None:
        """[0x62, 0xF1, 0x90, 0x01, 0x02] → {0xF190: b'\x01\x02'}."""
        response = bytes([0x62, 0xF1, 0x90, 0x01, 0x02])
        svc = _did_service(response)
        result = svc.read(0xF190)
        assert 0xF190 in result
        assert result[0xF190] == b"\x01\x02"

    def test_single_did_response_single_byte_data(self) -> None:
        response = bytes([0x62, 0x00, 0x01, 0xFF])
        svc = _did_service(response)
        result = svc.read(0x0001)
        assert result[0x0001] == b"\xff"

    def test_empty_data_did(self) -> None:
        """Response with DID but no data bytes → empty bytes."""
        response = bytes([0x62, 0xAB, 0xCD])
        svc = _did_service(response)
        result = svc.read(0xABCD)
        assert result[0xABCD] == b""
