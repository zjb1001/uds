"""UDS Security Access service (0x27)."""

from __future__ import annotations

from .client import UdsClient

_SID_SA = 0x27
# XOR mask used by the simplified security access algorithm.
# NOTE: This is a demonstration-grade algorithm. Production deployments
# should use a cryptographically secure key derivation function (e.g.
# HMAC-SHA256) as described in the project's security design document.
_XOR_MASK = bytes([0xAB, 0xCD, 0x12, 0x34])


class UdsSecurity:
    """UDS Security Access (Service 0x27) helper.

    Parameters
    ----------
    client:
        An open (or soon-to-be-open) :class:`~uds.client.UdsClient`.
    """

    def __init__(self, client: UdsClient) -> None:
        self._client = client

    # ── key algorithm ─────────────────────────────────────────────────────

    def compute_key(self, seed: bytes) -> bytes:
        """Compute the security key from *seed* using the XOR algorithm.

        The algorithm is ``key[i] = seed[i] XOR mask[i % 4]`` where
        ``mask = {0xAB, 0xCD, 0x12, 0x34}``.

        Parameters
        ----------
        seed:
            Seed bytes received from the ECU.

        Returns
        -------
        bytes
            Computed key (same length as *seed*).
        """
        return bytes(b ^ _XOR_MASK[i % 4] for i, b in enumerate(seed))

    # ── unlock sequence ───────────────────────────────────────────────────

    def unlock(self, level: int = 1) -> bool:
        """Perform the full seed/key Security Access exchange.

        Parameters
        ----------
        level:
            Security level (odd sub-function for seed request, level+1 for
            key send).  Default is ``1`` (most common level).

        Returns
        -------
        bool
            ``True`` if the ECU granted access; ``False`` is not returned
            in practice — a failed key raises :class:`~uds.exceptions.UdsNrcError`.

        Notes
        -----
        If the ECU responds with an all-zero seed, the session is already
        unlocked and this method returns ``True`` immediately.
        """
        # Request seed
        seed_request = bytes([_SID_SA, level])
        seed_response = self._client._request(seed_request)

        # Positive response: [0x67, level, seed...]
        if len(seed_response) < 2 or seed_response[0] != 0x67:
            from .exceptions import UdsProtocolError  # noqa: PLC0415

            raise UdsProtocolError(f"Unexpected SA seed response: {seed_response.hex()}")

        seed = seed_response[2:]

        # All-zero seed means already unlocked
        if all(b == 0 for b in seed):
            return True

        key = self.compute_key(seed)

        # Send key
        key_request = bytes([_SID_SA, level + 1]) + key
        key_response = self._client._request(key_request)

        # Positive response: [0x67, level+1]
        if len(key_response) < 2 or key_response[0] != 0x67:
            from .exceptions import UdsProtocolError  # noqa: PLC0415

            raise UdsProtocolError(f"Unexpected SA key response: {key_response.hex()}")

        return True
