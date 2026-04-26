"""Integration tests for ISO-TP transport over vcan0."""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "tools"))

from uds.transport import IsoTpTransport


@pytest.mark.integration
class TestIsoTpTransportLifecycle:
    """Transport open/close lifecycle on vcan0 (skipped without hardware)."""

    def test_open_and_close(self, skip_if_no_vcan: None) -> None:  # noqa: ARG002
        """IsoTpTransport can be opened and closed without raising."""
        transport = IsoTpTransport("vcan0", tx_id=0x601, rx_id=0x681)
        transport.open()
        transport.close()

    def test_context_manager_open_close(self, skip_if_no_vcan: None) -> None:  # noqa: ARG002
        """IsoTpTransport context manager opens and closes cleanly."""
        with IsoTpTransport("vcan0", tx_id=0x601, rx_id=0x681) as tp:
            assert tp._bus is not None
        assert tp._bus is None

    def test_close_without_open_is_safe(self) -> None:
        """close() on a never-opened transport must not raise."""
        transport = IsoTpTransport("vcan0", tx_id=0x601, rx_id=0x681)
        transport.close()  # should be a no-op

    def test_double_close_is_safe(self, skip_if_no_vcan: None) -> None:  # noqa: ARG002
        """Calling close() twice must not raise."""
        transport = IsoTpTransport("vcan0", tx_id=0x601, rx_id=0x681)
        transport.open()
        transport.close()
        transport.close()  # second close must be a no-op


@pytest.mark.integration
class TestUdsClientLifecycle:
    """UdsClient open/close lifecycle (skipped without vcan0)."""

    def test_client_open_close(self, uds_client: object) -> None:
        """The uds_client fixture provides an open client; it should close cleanly."""
        # uds_client fixture already called open(); close is called in teardown
        assert uds_client is not None
