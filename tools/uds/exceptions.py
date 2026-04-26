"""UDS exception hierarchy."""

from __future__ import annotations


class UdsError(Exception):
    """Base exception for all UDS errors."""


class UdsNrcError(UdsError):
    """Raised when the ECU returns a negative response (0x7F)."""

    def __init__(self, nrc: object, service_id: int, message: str = "") -> None:
        self.nrc = nrc
        self.service_id = service_id
        msg = message or f"NRC {nrc!r} for service 0x{service_id:02X}"
        super().__init__(msg)


class UdsTimeoutError(UdsError):
    """Raised when no response is received within the timeout."""


class UdsProtocolError(UdsError):
    """Raised when an unexpected or malformed UDS message is received."""
