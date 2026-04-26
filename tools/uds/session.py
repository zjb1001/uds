"""UDS session-layer services (0x10 DSC, 0x3E Tester Present)."""

from __future__ import annotations

import struct

from .client import UdsClient

_SID_DSC = 0x10
_SID_TP = 0x3E


class UdsSession:
    """UDS session management services.

    Parameters
    ----------
    client:
        An open (or soon-to-be-open) :class:`~uds.client.UdsClient`.
    """

    def __init__(self, client: UdsClient) -> None:
        self._client = client

    def change_session(self, session_type: int) -> dict:
        """Send Diagnostic Session Control (Service 0x10).

        Parameters
        ----------
        session_type:
            Sub-function byte (e.g. ``0x01`` for defaultSession,
            ``0x02`` for programmingSession, ``0x03`` for extendedSession).

        Returns
        -------
        dict
            ``{'session_type': int, 'p2_ms': int, 'p2_star_ms': int}``
        """
        request = bytes([_SID_DSC, session_type])
        response = self._client._request(request)

        # Positive response: [0x50, session_type, p2_hi, p2_lo, p2star_hi, p2star_lo]
        if len(response) < 6 or response[0] != 0x50:
            from .exceptions import UdsProtocolError  # noqa: PLC0415

            raise UdsProtocolError(f"Unexpected DSC response: {response.hex()}")

        p2_ms = struct.unpack(">H", response[2:4])[0]
        p2_star_raw = struct.unpack(">H", response[4:6])[0]
        # P2* is transmitted in units of 10 ms
        p2_star_ms = p2_star_raw * 10

        return {
            "session_type": response[1],
            "p2_ms": p2_ms,
            "p2_star_ms": p2_star_ms,
        }

    def tester_present(self, suppress: bool = False) -> None:
        """Send Tester Present (Service 0x3E) to keep the session alive.

        Parameters
        ----------
        suppress:
            If ``True``, the sub-function byte has bit 7 set (suppress positive
            response), meaning no response is expected from the ECU.
        """
        sub_fn = 0x80 if suppress else 0x00
        request = bytes([_SID_TP, sub_fn])

        if suppress:
            # No response expected; send and return
            self._client._transport.send(request)
            return

        self._client._request(request)
