"""High-level UDS client built on top of :class:`IsoTpTransport`."""

from __future__ import annotations

from .exceptions import UdsNrcError, UdsTimeoutError
from .nrc import UdsNrc
from .transport import IsoTpTransport

# Service IDs used internally
_SID_NRC = 0x7F

# 0x78 = RESPONSE_PENDING
_NRC_RESPONSE_PENDING = 0x78
_MAX_PENDING_RETRIES = 3


class UdsClient:
    """UDS client that communicates with a single ECU.

    Parameters
    ----------
    channel:
        CAN interface name (e.g. ``"vcan0"``).
    ecu_id:
        Logical ECU identifier.  The physical CAN IDs are derived as:
        ``tx_id = 0x600 + ecu_id``, ``rx_id = 0x680 + ecu_id``.
    timeout:
        Request/response timeout in seconds.
    """

    def __init__(self, channel: str, ecu_id: int, timeout: float = 2.0) -> None:
        self._channel = channel
        self._ecu_id = ecu_id
        self._timeout = timeout
        tx_id = 0x600 + ecu_id
        rx_id = 0x680 + ecu_id
        self._transport = IsoTpTransport(channel, tx_id, rx_id, timeout)

    # ── lifecycle ─────────────────────────────────────────────────────────

    def open(self) -> None:
        """Open the underlying CAN transport."""
        self._transport.open()

    def close(self) -> None:
        """Close the underlying CAN transport."""
        self._transport.close()

    # ── context manager ───────────────────────────────────────────────────

    def __enter__(self) -> UdsClient:
        self.open()
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    # ── internal request/response ─────────────────────────────────────────

    def _request(self, data: bytes) -> bytes:
        """Send *data* and return the positive response payload.

        Parameters
        ----------
        data:
            Raw UDS request bytes (service ID + parameters).

        Returns
        -------
        bytes
            Positive response payload (service ID + data).

        Raises
        ------
        UdsNrcError
            If the ECU replies with a negative response (0x7F …) that is not
            ``RESPONSE_PENDING`` (0x78), or if pending retries are exhausted.
        UdsTimeoutError
            If no response is received within the configured timeout.
        """
        self._transport.send(data)

        for _attempt in range(_MAX_PENDING_RETRIES + 1):
            try:
                response = self._transport.recv()
            except UdsTimeoutError:
                raise

            if len(response) >= 3 and response[0] == _SID_NRC:
                service_id = response[1]
                nrc_byte = response[2]
                if nrc_byte == _NRC_RESPONSE_PENDING:
                    # ECU is still processing; wait for the real response
                    continue
                try:
                    nrc = UdsNrc(nrc_byte)
                except ValueError:
                    nrc = nrc_byte  # type: ignore[assignment]
                raise UdsNrcError(nrc, service_id)

            return response

        raise UdsNrcError(
            UdsNrc.RESPONSE_PENDING,
            data[0] if data else 0,
            "ECU kept responding with RESPONSE_PENDING",
        )
