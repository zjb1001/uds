"""Flash performance benchmark integration tests.

Measures flash download and upload throughput (bytes/second) over vcan0
using the Python EcuSimulator and UdsFlashService.

The target from the project roadmap is ≥ 10 KB/s.

All tests are skipped when vcan0 is not available.
"""

from __future__ import annotations

import os
import sys
import time
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent.parent / "tools"))

from uds import UdsClient
from uds.ecusim import EcuSimulator
from uds.flash import UdsFlashService

# Throughput threshold from the project specification (bytes per second)
_MIN_THROUGHPUT_BPS = 10 * 1024  # 10 KB/s

# Payload sizes for benchmark runs
_SMALL_PAYLOAD = 4 * 1024  # 4 KB
_MEDIUM_PAYLOAD = 32 * 1024  # 32 KB


# ── fixtures ──────────────────────────────────────────────────────────────


@pytest.fixture
def flash_env(skip_if_no_vcan: None):  # noqa: ARG001
    """Start an EcuSimulator (ECU 3) and open a UdsClient for flash tests."""
    sim = EcuSimulator("vcan0", ecu_id=3)
    sim.start()
    client = UdsClient("vcan0", ecu_id=3, timeout=10.0)
    client.open()
    flash = UdsFlashService(client)
    yield sim, client, flash
    client.close()
    sim.stop()


# ── helpers ───────────────────────────────────────────────────────────────


def _measure_download(flash: UdsFlashService, data: bytes) -> float:
    """Return download throughput in bytes/second."""
    t0 = time.perf_counter()
    flash.download(address=0x00000000, data=data)
    elapsed = time.perf_counter() - t0
    return len(data) / elapsed


def _measure_upload(flash: UdsFlashService, length: int) -> float:
    """Return upload throughput in bytes/second."""
    t0 = time.perf_counter()
    flash.upload(address=0x00000000, length=length)
    elapsed = time.perf_counter() - t0
    return length / elapsed


# ── benchmark tests ───────────────────────────────────────────────────────


@pytest.mark.integration
@pytest.mark.slow
class TestFlashDownloadThroughput:
    """Verify that flash download meets the ≥ 10 KB/s specification."""

    def test_download_4kb_meets_spec(self, flash_env: object) -> None:
        """Downloading 4 KB of data should achieve ≥ 10 KB/s."""
        _, _, flash = flash_env
        data = os.urandom(_SMALL_PAYLOAD)
        throughput = _measure_download(flash, data)
        kb_s = throughput / 1024
        assert throughput >= _MIN_THROUGHPUT_BPS, (
            f"Download throughput {kb_s:.1f} KB/s is below the 10 KB/s target"
        )

    def test_download_32kb_meets_spec(self, flash_env: object) -> None:
        """Downloading 32 KB of data should achieve ≥ 10 KB/s."""
        _, _, flash = flash_env
        data = os.urandom(_MEDIUM_PAYLOAD)
        throughput = _measure_download(flash, data)
        kb_s = throughput / 1024
        assert throughput >= _MIN_THROUGHPUT_BPS, (
            f"Download throughput {kb_s:.1f} KB/s is below the 10 KB/s target"
        )

    def test_download_data_integrity(self, flash_env: object) -> None:
        """Data written via download can be read back correctly via upload."""
        _, _, flash = flash_env
        data = os.urandom(_SMALL_PAYLOAD)
        flash.download(address=0x00000000, data=data)
        readback = flash.upload(address=0x00000000, length=len(data))
        assert readback == data, "Round-trip download→upload data mismatch"


@pytest.mark.integration
@pytest.mark.slow
class TestFlashUploadThroughput:
    """Verify that flash upload meets the ≥ 10 KB/s specification."""

    def test_upload_4kb_meets_spec(self, flash_env: object) -> None:
        """Uploading 4 KB of data should achieve ≥ 10 KB/s."""
        _, _, flash = flash_env
        # Pre-populate flash so upload returns meaningful data
        data = os.urandom(_SMALL_PAYLOAD)
        flash.download(address=0x00000000, data=data)

        throughput = _measure_upload(flash, _SMALL_PAYLOAD)
        kb_s = throughput / 1024
        assert throughput >= _MIN_THROUGHPUT_BPS, (
            f"Upload throughput {kb_s:.1f} KB/s is below the 10 KB/s target"
        )

    def test_upload_32kb_meets_spec(self, flash_env: object) -> None:
        """Uploading 32 KB of data should achieve ≥ 10 KB/s."""
        _, _, flash = flash_env
        data = os.urandom(_MEDIUM_PAYLOAD)
        flash.download(address=0x00000000, data=data)

        throughput = _measure_upload(flash, _MEDIUM_PAYLOAD)
        kb_s = throughput / 1024
        assert throughput >= _MIN_THROUGHPUT_BPS, (
            f"Upload throughput {kb_s:.1f} KB/s is below the 10 KB/s target"
        )


@pytest.mark.integration
class TestFlashServiceProtocol:
    """Functional correctness tests for flash services 0x34/0x35/0x36/0x37."""

    def test_download_and_upload_roundtrip(self, flash_env: object) -> None:
        """A full download→upload round-trip preserves data exactly."""
        _, _, flash = flash_env
        original = bytes(range(256)) * 4  # 1 KB with known pattern
        flash.download(address=0x00000000, data=original)
        received = flash.upload(address=0x00000000, length=len(original))
        assert received == original

    def test_download_invalid_address_raises(self, flash_env: object) -> None:
        """Downloading to an address outside the flash region raises an error."""
        from uds.exceptions import UdsNrcError

        _, _, flash = flash_env
        with pytest.raises((UdsNrcError, Exception)):
            flash.download(address=0xFFFF0000, data=b"\x00" * 64)

    def test_sequential_downloads(self, flash_env: object) -> None:
        """Multiple sequential download sessions each complete successfully."""
        _, _, flash = flash_env
        for i in range(3):
            data = bytes([i & 0xFF] * 512)
            flash.download(address=0x00000000, data=data)
            readback = flash.upload(address=0x00000000, length=len(data))
            assert readback == data, f"Round-trip failed on iteration {i}"

    def test_progress_callback_called(self, flash_env: object) -> None:
        """The on_progress callback is invoked during download."""
        _, _, flash = flash_env
        calls: list[tuple[int, int]] = []

        def on_progress(sent: int, total: int) -> None:
            calls.append((sent, total))

        data = os.urandom(1024)
        flash.download(address=0x00000000, data=data, on_progress=on_progress)
        assert len(calls) > 0
        # Final call should report sent == total
        last_sent, last_total = calls[-1]
        assert last_sent == last_total == len(data)
