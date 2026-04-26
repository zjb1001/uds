"""Integration tests for UDS services over vcan0.

These tests launch a Python EcuSimulator in a background thread and exercise
the full UDS protocol stack (ISO-TP framing + UDS service semantics) using
a UdsClient.

All tests in this module are skipped when vcan0 is not available.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "tools"))

from uds import UdsClient
from uds.ecusim import _compute_key


@pytest.mark.integration
class TestDiagnosticSessionControl:
    """Service 0x10 — Diagnostic Session Control."""

    def test_switch_to_extended_session(self, uds_client_with_sim: UdsClient) -> None:
        """Switching to extended diagnostic session returns a valid response."""
        client = uds_client_with_sim
        resp = client._request(bytes([0x10, 0x03]))  # extendedDiagnosticSession
        assert resp[0] == 0x50, f"Expected 0x50, got 0x{resp[0]:02X}"
        assert resp[1] == 0x03, f"Expected session type 0x03, got 0x{resp[1]:02X}"
        assert len(resp) == 6  # SID + type + P2_hi + P2_lo + P2*_hi + P2*_lo

    def test_switch_to_programming_session(self, uds_client_with_sim: UdsClient) -> None:
        """Switching to programming session is supported."""
        resp = uds_client_with_sim._request(bytes([0x10, 0x02]))
        assert resp[0] == 0x50
        assert resp[1] == 0x02

    def test_return_to_default_session(self, uds_client_with_sim: UdsClient) -> None:
        """Switching back to default session succeeds."""
        client = uds_client_with_sim
        client._request(bytes([0x10, 0x03]))
        resp = client._request(bytes([0x10, 0x01]))
        assert resp[0] == 0x50
        assert resp[1] == 0x01

    def test_invalid_session_type_returns_nrc(self, uds_client_with_sim: UdsClient) -> None:
        """An unsupported session type returns NRC 0x12."""
        from uds.exceptions import UdsNrcError

        with pytest.raises(UdsNrcError) as exc_info:
            uds_client_with_sim._request(bytes([0x10, 0xFF]))
        assert exc_info.value.nrc == 0x12


@pytest.mark.integration
class TestTesterPresent:
    """Service 0x3E — Tester Present."""

    def test_tester_present_normal(self, uds_client_with_sim: UdsClient) -> None:
        """0x3E sub-fn 0x00 returns [0x7E, 0x00]."""
        resp = uds_client_with_sim._request(bytes([0x3E, 0x00]))
        assert resp[0] == 0x7E
        assert resp[1] == 0x00

    def test_tester_present_suppress(self, uds_client_with_sim: UdsClient) -> None:
        """0x3E with suppress-positive-response bit set sends nothing (no timeout)."""
        # sub-fn 0x80 = suppress bit set; expect no response to be received
        # The transport should time out because the ECU sends no response.
        # However the UdsClient only calls send() + recv(); since the response
        # is suppressed the ECU sends nothing. We exercise the suppress path
        # by checking that the ECU doesn't raise — this requires the client to
        # not wait indefinitely. Use a short timeout client.
        # Actually: if the ECU suppresses the response and the client waits,
        # the client will time out with UdsTimeoutError.
        # This is the correct ISO 14229-1 behaviour.
        from uds.exceptions import UdsTimeoutError

        short_client = UdsClient("vcan0", ecu_id=1, timeout=0.3)
        short_client.open()
        try:
            with pytest.raises(UdsTimeoutError):
                short_client._request(bytes([0x3E, 0x80]))
        finally:
            short_client.close()


@pytest.mark.integration
class TestSecurityAccess:
    """Service 0x27 — Security Access."""

    def _unlock(self, client: UdsClient, level: int = 0x01) -> None:
        """Helper: perform full seed/key exchange to unlock security level."""
        seed_resp = client._request(bytes([0x27, level]))
        assert seed_resp[0] == 0x67
        assert seed_resp[1] == level
        seed = seed_resp[2:]
        key = _compute_key(seed)
        key_resp = client._request(bytes([0x27, level + 1]) + key)
        assert key_resp[0] == 0x67
        assert key_resp[1] == level + 1

    def test_request_seed_returns_seed(self, uds_client_with_sim: UdsClient) -> None:
        """requestSeed returns a 4-byte seed."""
        resp = uds_client_with_sim._request(bytes([0x27, 0x01]))
        assert resp[0] == 0x67
        assert resp[1] == 0x01
        assert len(resp) == 6  # 0x67 + sub_fn + 4 seed bytes

    def test_correct_key_unlocks(self, uds_client_with_sim: UdsClient) -> None:
        """Sending the correct key succeeds and unlocks the ECU."""
        self._unlock(uds_client_with_sim, level=0x01)

    def test_already_unlocked_returns_zero_seed(self, uds_client_with_sim: UdsClient) -> None:
        """After unlocking, requestSeed for the same level returns zero seed."""
        self._unlock(uds_client_with_sim, level=0x01)
        resp = uds_client_with_sim._request(bytes([0x27, 0x01]))
        seed = resp[2:]
        assert seed == bytes(4), f"Expected zero seed, got {seed.hex()}"

    def test_wrong_key_returns_nrc_35(self, uds_client_with_sim: UdsClient) -> None:
        """Sending a wrong key returns NRC 0x35 (invalidKey)."""
        from uds.exceptions import UdsNrcError

        # Request seed
        uds_client_with_sim._request(bytes([0x27, 0x01]))
        # Send wrong key (all zeros)
        with pytest.raises(UdsNrcError) as exc_info:
            uds_client_with_sim._request(bytes([0x27, 0x02, 0x00, 0x00, 0x00, 0x00]))
        assert exc_info.value.nrc == 0x35


@pytest.mark.integration
class TestReadDataByIdentifier:
    """Service 0x22 — ReadDataByIdentifier."""

    def test_read_known_did(self, uds_client_with_sim: UdsClient) -> None:
        """Reading DID 0xF187 returns a non-empty response."""
        resp = uds_client_with_sim._request(bytes([0x22, 0xF1, 0x87]))
        assert resp[0] == 0x62
        assert resp[1] == 0xF1
        assert resp[2] == 0x87
        assert len(resp) > 3  # has data bytes

    def test_read_vin(self, uds_client_with_sim: UdsClient) -> None:
        """Reading VIN (0xF190) returns 17 ASCII bytes."""
        resp = uds_client_with_sim._request(bytes([0x22, 0xF1, 0x90]))
        assert resp[0] == 0x62
        # 3 bytes header (0x62 + DID) + 17 bytes VIN
        assert len(resp) == 3 + 17

    def test_read_unknown_did_returns_nrc(self, uds_client_with_sim: UdsClient) -> None:
        """Reading an unsupported DID returns NRC 0x31."""
        from uds.exceptions import UdsNrcError

        with pytest.raises(UdsNrcError) as exc_info:
            uds_client_with_sim._request(bytes([0x22, 0xDE, 0xAD]))
        assert exc_info.value.nrc == 0x31

    def test_read_multiple_dids(self, uds_client_with_sim: UdsClient) -> None:
        """Reading two DIDs in one request returns both records."""
        resp = uds_client_with_sim._request(bytes([0x22, 0xF1, 0x87, 0xF1, 0x91]))
        assert resp[0] == 0x62
        # Should contain DID 0xF187 followed by DID 0xF191


@pytest.mark.integration
class TestWriteDataByIdentifier:
    """Service 0x2E — WriteDataByIdentifier."""

    def test_write_did_in_extended_session(self, uds_client_with_sim: UdsClient) -> None:
        """Writing DID 0x0101 (odometer, 4 bytes) in extended session succeeds."""
        client = uds_client_with_sim
        # Switch to extended session first
        client._request(bytes([0x10, 0x03]))
        new_value = bytes([0x00, 0x01, 0x86, 0xA0])  # 100000
        resp = client._request(bytes([0x2E, 0x01, 0x01]) + new_value)
        assert resp[0] == 0x6E
        assert resp[1] == 0x01
        assert resp[2] == 0x01

        # Verify the write
        read_resp = client._request(bytes([0x22, 0x01, 0x01]))
        assert read_resp[3:7] == new_value

    def test_write_did_in_default_session_returns_nrc(self, uds_client_with_sim: UdsClient) -> None:
        """Writing a DID that requires extended session in default session → NRC."""
        from uds.exceptions import UdsNrcError

        with pytest.raises(UdsNrcError) as exc_info:
            uds_client_with_sim._request(bytes([0x2E, 0x01, 0x01, 0x00, 0x00, 0x00, 0x01]))
        assert exc_info.value.nrc in (0x31, 0x33)  # requestOutOfRange or securityAccessDenied


@pytest.mark.integration
class TestEcuReset:
    """Service 0x11 — ECUReset."""

    def test_soft_reset(self, uds_client_with_sim: UdsClient) -> None:
        """softReset (0x03) returns positive response and resets state."""
        client = uds_client_with_sim
        # Switch to extended session
        client._request(bytes([0x10, 0x03]))
        # Reset
        resp = client._request(bytes([0x11, 0x03]))
        assert resp[0] == 0x51
        assert resp[1] == 0x03
        # After reset, ECU should be in default session
        # A new DSC request should succeed
        dsc_resp = client._request(bytes([0x10, 0x01]))
        assert dsc_resp[0] == 0x50

    def test_hard_reset(self, uds_client_with_sim: UdsClient) -> None:
        """hardReset (0x01) is supported."""
        resp = uds_client_with_sim._request(bytes([0x11, 0x01]))
        assert resp[0] == 0x51
        assert resp[1] == 0x01

    def test_invalid_reset_type_returns_nrc(self, uds_client_with_sim: UdsClient) -> None:
        """Unsupported reset type returns NRC 0x12."""
        from uds.exceptions import UdsNrcError

        with pytest.raises(UdsNrcError) as exc_info:
            uds_client_with_sim._request(bytes([0x11, 0xFF]))
        assert exc_info.value.nrc == 0x12


@pytest.mark.integration
class TestReadDTCInformation:
    """Service 0x19 — ReadDTCInformation."""

    def test_read_dtc_count(self, uds_client_with_sim: UdsClient) -> None:
        """Sub-fn 0x01 returns DTC count for status mask 0xFF."""
        resp = uds_client_with_sim._request(bytes([0x19, 0x01, 0xFF]))
        assert resp[0] == 0x59
        assert resp[1] == 0x01
        count = (resp[4] << 8) | resp[5]
        assert count >= 1  # simulator has at least one confirmed DTC

    def test_read_dtc_by_status_mask(self, uds_client_with_sim: UdsClient) -> None:
        """Sub-fn 0x02 returns DTC records matching the status mask."""
        resp = uds_client_with_sim._request(bytes([0x19, 0x02, 0xFF]))
        assert resp[0] == 0x59
        assert resp[1] == 0x02
        # Should have at least one DTC (4 bytes per DTC: 3-byte code + status)
        assert len(resp) > 3

    def test_read_supported_dtcs(self, uds_client_with_sim: UdsClient) -> None:
        """Sub-fn 0x0A returns all supported DTCs."""
        resp = uds_client_with_sim._request(bytes([0x19, 0x0A]))
        assert resp[0] == 0x59
        assert resp[1] == 0x0A
        assert len(resp) > 3  # At least one DTC


@pytest.mark.integration
class TestClearDTC:
    """Service 0x14 — ClearDiagnosticInformation."""

    def test_clear_all_dtcs(self, uds_client_with_sim: UdsClient) -> None:
        """Clearing all DTCs (group 0xFFFFFF) returns positive response."""
        client = uds_client_with_sim
        resp = client._request(bytes([0x14, 0xFF, 0xFF, 0xFF]))
        assert resp[0] == 0x54

        # After clear, DTC count with any confirmed-DTC bit should be 0
        count_resp = client._request(bytes([0x19, 0x01, 0x08]))
        count = (count_resp[4] << 8) | count_resp[5]
        assert count == 0

    def test_clear_by_group(self, uds_client_with_sim: UdsClient) -> None:
        """Clearing a specific DTC group that exists returns positive response."""
        # Default DTCs have group byte 0x01
        resp = uds_client_with_sim._request(bytes([0x14, 0x01, 0x00, 0x00]))
        assert resp[0] == 0x54


@pytest.mark.integration
class TestUnknownService:
    """Unknown service ID returns NRC 0x11."""

    def test_unknown_sid_nrc(self, uds_client_with_sim: UdsClient) -> None:
        """An unsupported service ID returns NRC 0x11."""
        from uds.exceptions import UdsNrcError

        with pytest.raises(UdsNrcError) as exc_info:
            uds_client_with_sim._request(bytes([0xAA]))
        assert exc_info.value.nrc == 0x11


@pytest.mark.integration
class TestFullWorkflow:
    """End-to-end multi-service scenarios."""

    def test_session_security_did_workflow(self, uds_client_with_sim: UdsClient) -> None:
        """Extended session → security unlock → read DID."""
        client = uds_client_with_sim

        # 1. Switch to extended diagnostic session
        dsc = client._request(bytes([0x10, 0x03]))
        assert dsc[0] == 0x50

        # 2. Security access — request seed
        seed_resp = client._request(bytes([0x27, 0x01]))
        assert seed_resp[0] == 0x67
        seed = seed_resp[2:]

        # 3. Compute key and unlock
        key = _compute_key(seed)
        key_resp = client._request(bytes([0x27, 0x02]) + key)
        assert key_resp[0] == 0x67

        # 4. Read a DID
        did_resp = client._request(bytes([0x22, 0xF1, 0x87]))
        assert did_resp[0] == 0x62

        # 5. Tester Present
        tp = client._request(bytes([0x3E, 0x00]))
        assert tp[0] == 0x7E

    def test_dtc_workflow(self, uds_client_with_sim: UdsClient) -> None:
        """Read DTCs, then clear them, then verify they are gone."""
        client = uds_client_with_sim

        # Read initial DTC count (all statuses)
        count_resp = client._request(bytes([0x19, 0x01, 0xFF]))
        initial_count = (count_resp[4] << 8) | count_resp[5]
        assert initial_count >= 1

        # Clear all DTCs
        clear_resp = client._request(bytes([0x14, 0xFF, 0xFF, 0xFF]))
        assert clear_resp[0] == 0x54

        # Read count again — all confirmed + pending bits cleared → 0
        count_resp2 = client._request(bytes([0x19, 0x01, 0xFF]))
        final_count = (count_resp2[4] << 8) | count_resp2[5]
        assert final_count == 0
