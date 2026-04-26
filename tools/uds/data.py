"""UDS data services: Read/Write DID (0x22/0x2E), ECU Reset (0x11), Comm Control (0x28)."""

from __future__ import annotations

from .client import UdsClient

_SID_READ_DID = 0x22
_SID_WRITE_DID = 0x2E
_SID_ECU_RESET = 0x11
_SID_COMM_CTRL = 0x28


class UdsDidService:
    """UDS data identifier and ECU control services.

    Parameters
    ----------
    client:
        An open (or soon-to-be-open) :class:`~uds.client.UdsClient`.
    """

    def __init__(self, client: UdsClient) -> None:
        self._client = client

    # ── Read DID (0x22) ───────────────────────────────────────────────────

    def read(self, did_id: int | list[int]) -> dict[int, bytes]:
        """Read one or more Data Identifiers (Service 0x22).

        Parameters
        ----------
        did_id:
            A single DID integer or a list of DID integers.

        Returns
        -------
        dict[int, bytes]
            Mapping of DID → raw data bytes.
        """
        if isinstance(did_id, int):
            dids = [did_id]
        else:
            dids = list(did_id)

        request = bytearray([_SID_READ_DID])
        for did in dids:
            request += did.to_bytes(2, "big")

        response = self._client._request(bytes(request))

        # Positive response: [0x62, did_hi, did_lo, data..., (did_hi, did_lo, data...)...]
        if not response or response[0] != 0x62:
            from .exceptions import UdsProtocolError  # noqa: PLC0415

            raise UdsProtocolError(f"Unexpected ReadDID response: {response.hex()}")

        result: dict[int, bytes] = {}
        payload = response[1:]  # strip SID

        # For a single DID, the entire remaining payload is the data
        if len(dids) == 1:
            did = dids[0]
            if len(payload) >= 2:
                resp_did = (payload[0] << 8) | payload[1]
                result[resp_did] = bytes(payload[2:])
            return result

        # For multiple DIDs, parse sequentially. The ECU returns them in the
        # requested order; each record is [did_hi, did_lo, data...] where data
        # continues until the next DID or end of payload.
        idx = 0
        for i, did in enumerate(dids):
            if idx + 2 > len(payload):
                break
            resp_did = (payload[idx] << 8) | payload[idx + 1]
            idx += 2
            # Determine end of this DID's data by finding the next DID in payload
            next_did_start = len(payload)
            if i + 1 < len(dids):
                next_did = dids[i + 1]
                for j in range(idx, len(payload) - 1):
                    if (payload[j] << 8) | payload[j + 1] == next_did:
                        next_did_start = j
                        break
            result[resp_did] = bytes(payload[idx:next_did_start])
            idx = next_did_start

        return result

    # ── Write DID (0x2E) ──────────────────────────────────────────────────

    def write(self, did_id: int, data: bytes) -> None:
        """Write a Data Identifier (Service 0x2E).

        Parameters
        ----------
        did_id:
            Target DID.
        data:
            Data bytes to write.
        """
        request = bytes([_SID_WRITE_DID]) + did_id.to_bytes(2, "big") + data
        self._client._request(request)

    # ── ECU Reset (0x11) ──────────────────────────────────────────────────

    def ecu_reset(self, reset_type: int = 1) -> None:
        """Send ECU Reset (Service 0x11).

        Parameters
        ----------
        reset_type:
            Reset sub-function: ``1`` = hardReset, ``2`` = keyOffOnReset,
            ``3`` = softReset.
        """
        request = bytes([_SID_ECU_RESET, reset_type])
        self._client._request(request)

    # ── Communication Control (0x28) ──────────────────────────────────────

    def comm_control(self, control_type: int, comm_type: int) -> None:
        """Send Communication Control (Service 0x28).

        Parameters
        ----------
        control_type:
            Sub-function: ``0`` = enableRxAndTx, ``1`` = enableRxAndDisableTx,
            ``2`` = disableRxAndEnableTx, ``3`` = disableRxAndTx.
        comm_type:
            Communication type byte.
        """
        request = bytes([_SID_COMM_CTRL, control_type, comm_type])
        self._client._request(request)
