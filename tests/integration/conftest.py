"""pytest fixtures for integration tests."""

from __future__ import annotations

import socket
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "tools"))

from uds import UdsClient
from uds.transport import IsoTpTransport


def _vcan0_available() -> bool:
    """Return True if the vcan0 interface is up and accessible."""
    try:
        sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
        try:
            sock.bind(("vcan0",))
        finally:
            sock.close()
        return True
    except (OSError, AttributeError):
        return False


# ── fixtures ──────────────────────────────────────────────────────────────


@pytest.fixture(scope="session")
def vcan_available() -> bool:
    """Session-scoped fixture: True if vcan0 is available."""
    return _vcan0_available()


@pytest.fixture(autouse=False)
def skip_if_no_vcan(vcan_available: bool) -> None:
    """Skip the test if vcan0 is not available."""
    if not vcan_available:
        pytest.skip("vcan0 not available — skipping integration test")


@pytest.fixture
def uds_client(skip_if_no_vcan: None) -> UdsClient:  # noqa: ARG001
    """Create and yield a UdsClient connected to vcan0 ECU 1."""
    client = UdsClient("vcan0", ecu_id=1, timeout=1.0)
    client.open()
    yield client
    client.close()


@pytest.fixture
def iso_tp_transport(skip_if_no_vcan: None) -> IsoTpTransport:  # noqa: ARG001
    """Create and yield an IsoTpTransport connected to vcan0."""
    transport = IsoTpTransport("vcan0", tx_id=0x601, rx_id=0x681, timeout=1.0)
    transport.open()
    yield transport
    transport.close()
