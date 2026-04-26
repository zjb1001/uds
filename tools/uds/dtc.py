"""UDS DTC services: Read DTC Information (0x19) and Clear DTC (0x14)."""

from __future__ import annotations

from .client import UdsClient

_SID_READ_DTC = 0x19
_SID_CLEAR_DTC = 0x14

_SUB_REPORT_COUNT = 0x01
_SUB_REPORT_BY_STATUS = 0x02
_SUB_REPORT_ALL = 0x0A


class UdsDtcService:
    """UDS DTC (Diagnostic Trouble Code) services.

    Parameters
    ----------
    client:
        An open (or soon-to-be-open) :class:`~uds.client.UdsClient`.
    """

    def __init__(self, client: UdsClient) -> None:
        self._client = client

    # ── Read DTC count (sub-function 0x01) ────────────────────────────────

    def read_count(self, status_mask: int = 0xFF) -> int:
        """Report the number of DTCs matching *status_mask* (sub-function 0x01).

        Returns
        -------
        int
            Number of matching DTCs.
        """
        request = bytes([_SID_READ_DTC, _SUB_REPORT_COUNT, status_mask])
        response = self._client._request(request)

        # Response: [0x59, 0x01, status_avail_mask, dtc_cnt_hi, dtc_cnt_lo]
        if len(response) < 5 or response[0] != 0x59 or response[1] != _SUB_REPORT_COUNT:
            from .exceptions import UdsProtocolError  # noqa: PLC0415

            raise UdsProtocolError(f"Unexpected ReadDTCCount response: {response.hex()}")

        return (response[3] << 8) | response[4]

    # ── Read DTC by status (sub-function 0x02) ────────────────────────────

    def read_by_status(self, status_mask: int = 0xFF) -> list[dict]:
        """Report DTCs matching *status_mask* with their status bytes (sub-function 0x02).

        Returns
        -------
        list[dict]
            List of ``{'code': int, 'status': int}`` dicts.
        """
        request = bytes([_SID_READ_DTC, _SUB_REPORT_BY_STATUS, status_mask])
        response = self._client._request(request)

        # Response: [0x59, 0x02, status_avail_mask, dtc1_b2, dtc1_b1, dtc1_b0, status1, ...]
        if len(response) < 3 or response[0] != 0x59 or response[1] != _SUB_REPORT_BY_STATUS:
            from .exceptions import UdsProtocolError  # noqa: PLC0415

            raise UdsProtocolError(f"Unexpected ReadDTCByStatus response: {response.hex()}")

        return self._parse_dtc_records(response[3:])

    # ── Read all DTCs (sub-function 0x0A) ─────────────────────────────────

    def read_all(self) -> list[dict]:
        """Report all supported DTCs (sub-function 0x0A).

        Returns
        -------
        list[dict]
            List of ``{'code': int, 'status': int}`` dicts.
        """
        request = bytes([_SID_READ_DTC, _SUB_REPORT_ALL])
        response = self._client._request(request)

        # Response: [0x59, 0x0A, status_avail_mask, dtc_b2, dtc_b1, dtc_b0, status, ...]
        if len(response) < 3 or response[0] != 0x59 or response[1] != _SUB_REPORT_ALL:
            from .exceptions import UdsProtocolError  # noqa: PLC0415

            raise UdsProtocolError(f"Unexpected ReadAllDTC response: {response.hex()}")

        return self._parse_dtc_records(response[3:])

    # ── Clear DTC (service 0x14) ──────────────────────────────────────────

    def clear(self, group: int = 0xFFFFFF) -> None:
        """Clear diagnostic information for the given DTC group (Service 0x14).

        Parameters
        ----------
        group:
            3-byte DTC group number.  ``0xFFFFFF`` means "all DTCs".
        """
        request = bytes([_SID_CLEAR_DTC]) + group.to_bytes(3, "big")
        self._client._request(request)

    # ── helpers ───────────────────────────────────────────────────────────

    @staticmethod
    def _parse_dtc_records(data: bytes) -> list[dict]:
        """Parse raw DTC records: each record is 4 bytes [b2, b1, b0, status]."""
        records: list[dict] = []
        idx = 0
        while idx + 4 <= len(data):
            code = (data[idx] << 16) | (data[idx + 1] << 8) | data[idx + 2]
            status = data[idx + 3]
            records.append({"code": code, "status": status})
            idx += 4
        return records
