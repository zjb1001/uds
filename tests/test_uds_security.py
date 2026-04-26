"""Unit tests for UdsSecurity.compute_key()."""

from __future__ import annotations

import sys
from pathlib import Path
from unittest.mock import MagicMock

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))

from uds.security import UdsSecurity


def _make_security() -> UdsSecurity:
    """Create a UdsSecurity instance with a mock client (no CAN needed)."""
    mock_client = MagicMock()
    return UdsSecurity(mock_client)


@pytest.mark.unit
class TestComputeKey:
    """compute_key() XOR algorithm tests."""

    def test_all_zeros_seed(self) -> None:
        """seed=0x00000000 → key=0xABCD1234."""
        sec = _make_security()
        key = sec.compute_key(b"\x00\x00\x00\x00")
        assert key == b"\xab\xcd\x12\x34"

    def test_all_ones_seed(self) -> None:
        """seed=0xFFFFFFFF → key=0x5432EDCB."""
        sec = _make_security()
        key = sec.compute_key(b"\xff\xff\xff\xff")
        assert key == b"\x54\x32\xed\xcb"

    def test_mask_as_seed(self) -> None:
        """seed=0xABCD1234 → key=0x00000000 (XOR with itself)."""
        sec = _make_security()
        key = sec.compute_key(b"\xab\xcd\x12\x34")
        assert key == b"\x00\x00\x00\x00"

    def test_key_length_matches_seed(self) -> None:
        """Output length equals input length."""
        sec = _make_security()
        for length in (1, 2, 3, 4, 5, 8, 16):
            seed = bytes(range(length))
            key = sec.compute_key(seed)
            assert len(key) == length

    def test_single_byte_seed(self) -> None:
        """seed=0x00 → key=0xAB (first mask byte)."""
        sec = _make_security()
        assert sec.compute_key(b"\x00") == b"\xab"

    def test_single_byte_seed_ff(self) -> None:
        """seed=0xFF → key=0x54 (0xFF XOR 0xAB)."""
        sec = _make_security()
        assert sec.compute_key(b"\xff") == b"\x54"

    def test_mask_wraps_for_longer_seeds(self) -> None:
        """Mask cycles for seeds longer than 4 bytes."""
        sec = _make_security()
        seed = b"\x00" * 8
        key = sec.compute_key(seed)
        # mask repeats: AB CD 12 34 AB CD 12 34
        assert key == b"\xab\xcd\x12\x34\xab\xcd\x12\x34"

    def test_xor_is_reversible(self) -> None:
        """compute_key(compute_key(seed)) == seed."""
        sec = _make_security()
        seed = bytes([0x12, 0x34, 0x56, 0x78])
        assert sec.compute_key(sec.compute_key(seed)) == seed

    def test_empty_seed_returns_empty(self) -> None:
        sec = _make_security()
        assert sec.compute_key(b"") == b""
