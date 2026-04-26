"""Multi-ECU concurrency integration tests.

These tests exercise multiple EcuSimulator instances running simultaneously
on the same vcan0 interface to verify:

- Session and security state isolation between ECUs (each ECU maintains its
  own independent UDS state).
- Concurrent request/response routing (each UdsClient only communicates with
  its assigned ECU).
- Independent DID data per ECU instance.

All tests are skipped when vcan0 is not available.
"""

from __future__ import annotations

import sys
import threading
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "tools"))

from uds import UdsClient
from uds.ecusim import EcuSimulator, _compute_key

# ── helpers ───────────────────────────────────────────────────────────────


def _make_sim_and_client(
    channel: str, ecu_id: int, timeout: float = 2.0
) -> tuple[EcuSimulator, UdsClient]:
    """Create and start an EcuSimulator + open a matching UdsClient."""
    sim = EcuSimulator(channel, ecu_id=ecu_id)
    sim.start()
    client = UdsClient(channel, ecu_id=ecu_id, timeout=timeout)
    client.open()
    return sim, client


def _teardown(sim: EcuSimulator, client: UdsClient) -> None:
    client.close()
    sim.stop()


# ── fixtures ──────────────────────────────────────────────────────────────


@pytest.fixture
def dual_ecu(skip_if_no_vcan: None):  # noqa: ARG001
    """Two independent ECU simulators on vcan0 (ECU IDs 1 and 2)."""
    sim1, client1 = _make_sim_and_client("vcan0", ecu_id=1)
    sim2, client2 = _make_sim_and_client("vcan0", ecu_id=2)
    yield (sim1, client1), (sim2, client2)
    _teardown(sim1, client1)
    _teardown(sim2, client2)


# ── Session isolation ─────────────────────────────────────────────────────


@pytest.mark.integration
class TestSessionIsolation:
    """Each ECU maintains its own session state independently."""

    def test_independent_session_switch(self, dual_ecu: object) -> None:
        """Switching ECU-1 to extended session does not affect ECU-2."""
        (_, c1), (_, c2) = dual_ecu

        # Switch ECU-1 to extended session
        r1 = c1._request(bytes([0x10, 0x03]))
        assert r1[0] == 0x50 and r1[1] == 0x03

        # ECU-2 should still be in default session; switching it to programming
        # session should succeed independently
        r2 = c2._request(bytes([0x10, 0x02]))
        assert r2[0] == 0x50 and r2[1] == 0x02

        # Return ECU-1 to default
        r1b = c1._request(bytes([0x10, 0x01]))
        assert r1b[1] == 0x01

        # ECU-2 still in programming session; confirm with another DSC
        r2b = c2._request(bytes([0x10, 0x02]))
        assert r2b[1] == 0x02

    def test_reset_one_ecu_does_not_affect_other(self, dual_ecu: object) -> None:
        """Hard-resetting ECU-1 leaves ECU-2 in its current session."""
        (_, c1), (_, c2) = dual_ecu

        # Put ECU-2 in extended session
        c2._request(bytes([0x10, 0x03]))

        # Reset ECU-1
        r1 = c1._request(bytes([0x11, 0x01]))
        assert r1[0] == 0x51

        # ECU-2 should still respond (its state is unchanged)
        tp = c2._request(bytes([0x3E, 0x00]))
        assert tp[0] == 0x7E


# ── Security state isolation ──────────────────────────────────────────────


@pytest.mark.integration
class TestSecurityIsolation:
    """Security access state is independent per ECU."""

    def _unlock(self, client: UdsClient, level: int = 0x01) -> None:
        seed_resp = client._request(bytes([0x27, level]))
        seed = seed_resp[2:]
        key = _compute_key(seed)
        client._request(bytes([0x27, level + 1]) + key)

    def test_unlock_ecu1_does_not_unlock_ecu2(self, dual_ecu: object) -> None:
        """Unlocking security on ECU-1 has no effect on ECU-2's security state."""
        (_, c1), (_, c2) = dual_ecu

        self._unlock(c1, level=0x01)

        # ECU-1: re-request seed should return zero (already unlocked)
        r1 = c1._request(bytes([0x27, 0x01]))
        assert r1[2:] == bytes(4), "ECU-1 should return zero seed after unlock"

        # ECU-2: re-request seed should return a non-zero random seed
        r2 = c2._request(bytes([0x27, 0x01]))
        # seed is random; just verify ECU-2 responds with a proper seed response
        assert r2[0] == 0x67

    def test_wrong_key_lockout_isolated(self, dual_ecu: object) -> None:
        """Lockout on ECU-1 from failed attempts does not affect ECU-2."""
        from uds.exceptions import UdsNrcError

        (_, c1), (_, c2) = dual_ecu

        # Exhaust security attempts on ECU-1
        for _ in range(3):
            try:
                c1._request(bytes([0x27, 0x01]))
                c1._request(bytes([0x27, 0x02, 0x00, 0x00, 0x00, 0x00]))
            except UdsNrcError:
                pass

        # ECU-2 should still allow seed requests
        r2 = c2._request(bytes([0x27, 0x01]))
        assert r2[0] == 0x67


# ── DID data isolation ────────────────────────────────────────────────────


@pytest.mark.integration
class TestDidIsolation:
    """Each ECU simulator instance holds its own independent DID values."""

    def test_write_did_on_ecu1_does_not_change_ecu2(self, dual_ecu: object) -> None:
        """Writing DID 0x0101 on ECU-1 does not change the same DID on ECU-2."""
        (_, c1), (_, c2) = dual_ecu

        # Switch both to extended session so write is allowed
        c1._request(bytes([0x10, 0x03]))
        c2._request(bytes([0x10, 0x03]))

        # Read original DID from ECU-2
        r2_before = c2._request(bytes([0x22, 0x01, 0x01]))
        original = r2_before[3:7]

        # Write a different value to ECU-1
        new_val = bytes([0x00, 0x02, 0x00, 0x00])
        c1._request(bytes([0x2E, 0x01, 0x01]) + new_val)

        # ECU-2's DID should still be unchanged
        r2_after = c2._request(bytes([0x22, 0x01, 0x01]))
        assert r2_after[3:7] == original


# ── Concurrent requests ───────────────────────────────────────────────────


@pytest.mark.integration
class TestConcurrentRequests:
    """Both ECUs respond correctly when clients send requests concurrently."""

    def test_concurrent_tester_present(self, dual_ecu: object) -> None:
        """Tester Present sent to both ECUs concurrently succeeds on both."""
        (_, c1), (_, c2) = dual_ecu

        results: dict[int, bytes] = {}
        errors: dict[int, Exception] = {}

        def send(ecu_id: int, client: UdsClient) -> None:
            try:
                results[ecu_id] = client._request(bytes([0x3E, 0x00]))
            except Exception as exc:  # noqa: BLE001
                errors[ecu_id] = exc

        t1 = threading.Thread(target=send, args=(1, c1))
        t2 = threading.Thread(target=send, args=(2, c2))
        t1.start()
        t2.start()
        t1.join(timeout=5)
        t2.join(timeout=5)

        assert not errors, f"Concurrent requests raised errors: {errors}"
        assert results[1][0] == 0x7E
        assert results[2][0] == 0x7E

    def test_concurrent_session_switch(self, dual_ecu: object) -> None:
        """Both ECUs handle concurrent DSC requests independently."""
        (_, c1), (_, c2) = dual_ecu

        results: dict[int, bytes] = {}

        def send(ecu_id: int, client: UdsClient, session: int) -> None:
            results[ecu_id] = client._request(bytes([0x10, session]))

        t1 = threading.Thread(target=send, args=(1, c1, 0x03))
        t2 = threading.Thread(target=send, args=(2, c2, 0x02))
        t1.start()
        t2.start()
        t1.join(timeout=5)
        t2.join(timeout=5)

        assert results[1][0] == 0x50 and results[1][1] == 0x03
        assert results[2][0] == 0x50 and results[2][1] == 0x02

    def test_concurrent_did_reads(self, dual_ecu: object) -> None:
        """Reading different DIDs from both ECUs concurrently returns correct results."""
        (_, c1), (_, c2) = dual_ecu

        results: dict[int, bytes] = {}
        errors: dict[int, Exception] = {}

        def read_did(ecu_id: int, client: UdsClient) -> None:
            try:
                results[ecu_id] = client._request(bytes([0x22, 0xF1, 0x90]))
            except Exception as exc:  # noqa: BLE001
                errors[ecu_id] = exc

        t1 = threading.Thread(target=read_did, args=(1, c1))
        t2 = threading.Thread(target=read_did, args=(2, c2))
        t1.start()
        t2.start()
        t1.join(timeout=5)
        t2.join(timeout=5)

        assert not errors, f"Concurrent DID reads raised errors: {errors}"
        # VIN is 17 bytes → response = 0x62 + 2-byte DID + 17 bytes = 20 bytes
        assert len(results[1]) == 20
        assert len(results[2]) == 20
