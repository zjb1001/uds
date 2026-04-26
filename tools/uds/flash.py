"""UDS flash memory services: RequestDownload (0x34), RequestUpload (0x35),
TransferData (0x36), RequestTransferExit (0x37)."""

from __future__ import annotations

from collections.abc import Callable

from .client import UdsClient

_SID_REQUEST_DOWNLOAD = 0x34
_SID_REQUEST_UPLOAD = 0x35
_SID_TRANSFER_DATA = 0x36
_SID_TRANSFER_EXIT = 0x37


class UdsFlashService:
    """UDS flash programming services.

    Parameters
    ----------
    client:
        An open (or soon-to-be-open) :class:`~uds.client.UdsClient`.
    """

    def __init__(self, client: UdsClient) -> None:
        self._client = client

    # ── Download (0x34 + 0x36* + 0x37) ───────────────────────────────────

    def download(
        self,
        address: int,
        data: bytes,
        addr_len: int = 4,
        size_len: int = 2,
        on_progress: Callable[[int, int], None] | None = None,
    ) -> None:
        """Download *data* to the ECU starting at *address*.

        Parameters
        ----------
        address:
            Memory start address.
        data:
            Binary data to write.
        addr_len:
            Number of bytes used to encode *address* (1–4).
        size_len:
            Number of bytes used to encode the data length (1–4).
        on_progress:
            Optional callback ``(bytes_sent, total_bytes) -> None``.
        """
        max_block_len = self._request_download(address, len(data), addr_len, size_len)
        # max_block_len includes the 1-byte block sequence counter header
        chunk_size = max(1, max_block_len - 1)

        total = len(data)
        offset = 0
        block_sn = 1

        while offset < total:
            chunk = data[offset : offset + chunk_size]
            self._transfer_data(block_sn, chunk)
            offset += len(chunk)
            block_sn = (block_sn + 1) & 0xFF
            if on_progress:
                on_progress(offset, total)

        self._transfer_exit()

    # ── Upload (0x35 + 0x36* + 0x37) ─────────────────────────────────────

    def upload(
        self,
        address: int,
        length: int,
        addr_len: int = 4,
        size_len: int = 2,
        on_progress: Callable[[int, int], None] | None = None,
    ) -> bytes:
        """Upload *length* bytes from the ECU starting at *address*.

        Parameters
        ----------
        address:
            Memory start address.
        length:
            Number of bytes to read.
        addr_len:
            Number of bytes used to encode *address* (1–4).
        size_len:
            Number of bytes used to encode *length* (1–4).
        on_progress:
            Optional callback ``(bytes_received, total_bytes) -> None``.

        Returns
        -------
        bytes
            The uploaded data.
        """
        max_block_len = self._request_upload(address, length, addr_len, size_len)
        chunk_size = max(1, max_block_len - 1)

        result = bytearray()
        block_sn = 1

        while len(result) < length:
            chunk = self._transfer_data(block_sn, b"")
            result += chunk
            block_sn = (block_sn + 1) & 0xFF
            if on_progress:
                on_progress(len(result), length)

        self._transfer_exit()
        return bytes(result[:length])

    # ── private helpers ───────────────────────────────────────────────────

    def _request_download(
        self, address: int, size: int, addr_len: int, size_len: int
    ) -> int:
        """Send RequestDownload (0x34) and return maxBlockLength."""
        addr_and_len_fmt = ((size_len & 0x0F) << 4) | (addr_len & 0x0F)
        request = (
            bytes([_SID_REQUEST_DOWNLOAD, 0x00, addr_and_len_fmt])
            + address.to_bytes(addr_len, "big")
            + size.to_bytes(size_len, "big")
        )
        response = self._client._request(request)

        # Positive response: [0x74, len_format, max_block_len...]
        if not response or response[0] != 0x74:
            from .exceptions import UdsProtocolError  # noqa: PLC0415

            raise UdsProtocolError(f"Unexpected RequestDownload response: {response.hex()}")

        mbl_len = (response[1] >> 4) & 0x0F
        if len(response) < 2 + mbl_len:
            from .exceptions import UdsProtocolError  # noqa: PLC0415

            raise UdsProtocolError("RequestDownload response too short for maxBlockLength")

        max_block_len = int.from_bytes(response[2 : 2 + mbl_len], "big")
        return max_block_len

    def _request_upload(
        self, address: int, size: int, addr_len: int, size_len: int
    ) -> int:
        """Send RequestUpload (0x35) and return maxBlockLength."""
        addr_and_len_fmt = ((size_len & 0x0F) << 4) | (addr_len & 0x0F)
        request = (
            bytes([_SID_REQUEST_UPLOAD, 0x00, addr_and_len_fmt])
            + address.to_bytes(addr_len, "big")
            + size.to_bytes(size_len, "big")
        )
        response = self._client._request(request)

        # Positive response: [0x75, len_format, max_block_len...]
        if not response or response[0] != 0x75:
            from .exceptions import UdsProtocolError  # noqa: PLC0415

            raise UdsProtocolError(f"Unexpected RequestUpload response: {response.hex()}")

        mbl_len = (response[1] >> 4) & 0x0F
        if len(response) < 2 + mbl_len:
            from .exceptions import UdsProtocolError  # noqa: PLC0415

            raise UdsProtocolError("RequestUpload response too short for maxBlockLength")

        max_block_len = int.from_bytes(response[2 : 2 + mbl_len], "big")
        return max_block_len

    def _transfer_data(self, block_sn: int, data: bytes) -> bytes:
        """Send TransferData (0x36) and return the response data payload."""
        request = bytes([_SID_TRANSFER_DATA, block_sn & 0xFF]) + data
        response = self._client._request(request)

        # Positive response: [0x76, block_sn, data...]
        if not response or response[0] != 0x76:
            from .exceptions import UdsProtocolError  # noqa: PLC0415

            raise UdsProtocolError(f"Unexpected TransferData response: {response.hex()}")

        return bytes(response[2:])

    def _transfer_exit(self) -> None:
        """Send RequestTransferExit (0x37)."""
        response = self._client._request(bytes([_SID_TRANSFER_EXIT]))

        if not response or response[0] != 0x77:
            from .exceptions import UdsProtocolError  # noqa: PLC0415

            raise UdsProtocolError(f"Unexpected TransferExit response: {response.hex()}")
