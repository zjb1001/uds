"""Unit tests for UdsNrc enum."""

from __future__ import annotations

import pytest

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))

from uds.nrc import UdsNrc


@pytest.mark.unit
class TestUdsNrcValues:
    """All NRC enum values can be instantiated from their integer value."""

    @pytest.mark.parametrize(
        "value",
        [
            0x10, 0x11, 0x12, 0x13, 0x14,
            0x21, 0x22, 0x24, 0x25, 0x26,
            0x31, 0x33, 0x35, 0x36, 0x37,
            0x70, 0x71, 0x72, 0x73, 0x78,
            0x7E, 0x7F,
        ],
    )
    def test_all_values_instantiable(self, value: int) -> None:
        """Every defined NRC byte can be constructed via UdsNrc(value)."""
        nrc = UdsNrc(value)
        assert nrc.value == value

    def test_general_reject(self) -> None:
        assert UdsNrc.GENERAL_REJECT == 0x10

    def test_service_not_supported(self) -> None:
        assert UdsNrc.SERVICE_NOT_SUPPORTED == 0x11

    def test_invalid_key(self) -> None:
        assert UdsNrc.INVALID_KEY == 0x35

    def test_response_pending(self) -> None:
        assert UdsNrc.RESPONSE_PENDING == 0x78

    def test_service_not_supported_in_session(self) -> None:
        assert UdsNrc.SERVICE_NOT_SUPPORTED_IN_SESSION == 0x7F


@pytest.mark.unit
class TestFromByte:
    """UdsNrc.from_byte() behaviour."""

    def test_known_value_returns_enum(self) -> None:
        nrc = UdsNrc.from_byte(0x10)
        assert nrc is UdsNrc.GENERAL_REJECT

    def test_from_byte_0x22(self) -> None:
        assert UdsNrc.from_byte(0x22) is UdsNrc.CONDITIONS_NOT_CORRECT

    def test_from_byte_0x35(self) -> None:
        assert UdsNrc.from_byte(0x35) is UdsNrc.INVALID_KEY

    def test_from_byte_0x78(self) -> None:
        assert UdsNrc.from_byte(0x78) is UdsNrc.RESPONSE_PENDING

    def test_unknown_byte_raises_value_error(self) -> None:
        with pytest.raises(ValueError, match="Unknown NRC byte"):
            UdsNrc.from_byte(0x00)

    def test_unknown_byte_0xAA_raises(self) -> None:
        with pytest.raises(ValueError):
            UdsNrc.from_byte(0xAA)


@pytest.mark.unit
class TestDescription:
    """UdsNrc.description property."""

    def test_description_non_empty_general_reject(self) -> None:
        assert len(UdsNrc.GENERAL_REJECT.description) > 0

    def test_description_non_empty_invalid_key(self) -> None:
        assert len(UdsNrc.INVALID_KEY.description) > 0

    def test_description_is_string(self) -> None:
        assert isinstance(UdsNrc.CONDITIONS_NOT_CORRECT.description, str)

    @pytest.mark.parametrize(
        "nrc",
        list(UdsNrc),
    )
    def test_all_descriptions_non_empty(self, nrc: UdsNrc) -> None:
        """Every NRC member has a non-empty description."""
        assert len(nrc.description) > 0


@pytest.mark.unit
class TestStrRepresentation:
    """str() representation of UdsNrc."""

    def test_str_contains_name(self) -> None:
        s = str(UdsNrc.GENERAL_REJECT)
        assert "GENERAL_REJECT" in s

    def test_str_contains_hex_value(self) -> None:
        s = str(UdsNrc.GENERAL_REJECT)
        assert "0x10" in s

    def test_str_contains_description(self) -> None:
        s = str(UdsNrc.INVALID_KEY)
        assert len(s) > len("INVALID_KEY")
