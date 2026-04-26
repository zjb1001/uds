"""Unit tests for UDS session-layer message parsing logic."""

from __future__ import annotations

import struct
import sys
from pathlib import Path
from unittest.mock import MagicMock

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))

from uds.exceptions import UdsNrcError, UdsProtocolError
from uds.nrc import UdsNrc
from uds.session import UdsSession


def _session_with_response(response: bytes) -> UdsSession:
    """Return a UdsSession whose underlying client._request returns *response*."""
    mock_client = MagicMock()
    mock_client._request.return_value = response
    mock_client._transport = MagicMock()
    return UdsSession(mock_client)


# ── DSC response parsing ────────────────────────────────────────────────────


@pytest.mark.unit
class TestDscResponseParsing:
    """Diagnostic Session Control (0x10) response parsing."""

    def _build_dsc_response(
        self, session_type: int, p2_ms: int, p2_star_10ms: int
    ) -> bytes:
        """Build a 6-byte positive DSC response."""
        return struct.pack(">BBHH", 0x50, session_type, p2_ms, p2_star_10ms)

    def test_parse_default_session(self) -> None:
        response = self._build_dsc_response(0x01, 25, 250)
        svc = _session_with_response(response)
        result = svc.change_session(0x01)
        assert result["session_type"] == 0x01

    def test_parse_p2_ms(self) -> None:
        response = self._build_dsc_response(0x01, 50, 0)
        svc = _session_with_response(response)
        result = svc.change_session(0x01)
        assert result["p2_ms"] == 50

    def test_parse_p2_star_ms(self) -> None:
        # p2_star_10ms=300 → p2_star_ms = 300 * 10 = 3000
        response = self._build_dsc_response(0x01, 25, 300)
        svc = _session_with_response(response)
        result = svc.change_session(0x01)
        assert result["p2_star_ms"] == 3000

    def test_parse_programming_session(self) -> None:
        response = self._build_dsc_response(0x02, 25, 500)
        svc = _session_with_response(response)
        result = svc.change_session(0x02)
        assert result["session_type"] == 0x02

    def test_parse_extended_session(self) -> None:
        response = self._build_dsc_response(0x03, 25, 500)
        svc = _session_with_response(response)
        result = svc.change_session(0x03)
        assert result["session_type"] == 0x03

    def test_invalid_response_raises_protocol_error(self) -> None:
        # Wrong SID in response
        bad_response = bytes([0x40, 0x01, 0x00, 0x19, 0x00, 0xFA])
        svc = _session_with_response(bad_response)
        with pytest.raises(UdsProtocolError):
            svc.change_session(0x01)

    def test_short_response_raises_protocol_error(self) -> None:
        svc = _session_with_response(bytes([0x50, 0x01]))
        with pytest.raises(UdsProtocolError):
            svc.change_session(0x01)


# ── Tester Present response ─────────────────────────────────────────────────


@pytest.mark.unit
class TestTesterPresentResponse:
    """Tester Present (0x3E) response handling."""

    def test_valid_response_does_not_raise(self) -> None:
        """A response of [0x7E, 0x00] should be accepted silently."""
        mock_client = MagicMock()
        mock_client._request.return_value = bytes([0x7E, 0x00])
        mock_client._transport = MagicMock()
        svc = UdsSession(mock_client)
        svc.tester_present()  # must not raise

    def test_suppress_does_not_call_request(self) -> None:
        """With suppress=True the transport.send is called instead of _request."""
        mock_client = MagicMock()
        mock_client._transport = MagicMock()
        svc = UdsSession(mock_client)
        svc.tester_present(suppress=True)
        mock_client._transport.send.assert_called_once()
        mock_client._request.assert_not_called()


# ── NRC response raises UdsNrcError ────────────────────────────────────────


@pytest.mark.unit
class TestNrcResponseRaises:
    """The client raises UdsNrcError for negative responses."""

    def test_nrc_error_carries_nrc(self) -> None:
        mock_client = MagicMock()
        nrc_exc = UdsNrcError(UdsNrc.CONDITIONS_NOT_CORRECT, 0x10)
        mock_client._request.side_effect = nrc_exc
        mock_client._transport = MagicMock()
        svc = UdsSession(mock_client)
        with pytest.raises(UdsNrcError) as exc_info:
            svc.change_session(0x02)
        assert exc_info.value.nrc is UdsNrc.CONDITIONS_NOT_CORRECT

    def test_nrc_error_carries_service_id(self) -> None:
        mock_client = MagicMock()
        nrc_exc = UdsNrcError(UdsNrc.SERVICE_NOT_SUPPORTED, 0x10)
        mock_client._request.side_effect = nrc_exc
        mock_client._transport = MagicMock()
        svc = UdsSession(mock_client)
        with pytest.raises(UdsNrcError) as exc_info:
            svc.change_session(0x01)
        assert exc_info.value.service_id == 0x10

    def test_nrc_response_bytes_format(self) -> None:
        """Verify [0x7F, sid, nrc] byte structure maps to correct NRC."""
        nrc_byte = 0x22
        nrc = UdsNrc(nrc_byte)
        assert nrc is UdsNrc.CONDITIONS_NOT_CORRECT
        exc = UdsNrcError(nrc, 0x10)
        assert exc.nrc is UdsNrc.CONDITIONS_NOT_CORRECT
        assert exc.service_id == 0x10
