"""ISO 14229-1 Negative Response Code (NRC) definitions."""

from __future__ import annotations

from enum import IntEnum


class UdsNrc(IntEnum):
    """ISO 14229-1 Negative Response Codes (§A.1)."""

    GENERAL_REJECT = 0x10
    SERVICE_NOT_SUPPORTED = 0x11
    SUB_FUNCTION_NOT_SUPPORTED = 0x12
    INCORRECT_MSG_LEN_OR_FORMAT = 0x13
    RESPONSE_TOO_LONG = 0x14
    BUSY_REPEAT_REQUEST = 0x21
    CONDITIONS_NOT_CORRECT = 0x22
    REQUEST_SEQUENCE_ERROR = 0x24
    NO_RESPONSE_FROM_SUBNET = 0x25
    FAILURE_PREVENTS_EXEC = 0x26
    REQUEST_OUT_OF_RANGE = 0x31
    SECURITY_ACCESS_DENIED = 0x33
    INVALID_KEY = 0x35
    EXCEEDED_NUMBER_OF_ATTEMPTS = 0x36
    REQUIRED_TIME_DELAY_NOT_EXPIRED = 0x37
    UPLOAD_DOWNLOAD_NOT_ACCEPTED = 0x70
    TRANSFER_DATA_SUSPENDED = 0x71
    GENERAL_PROGRAMMING_FAILURE = 0x72
    WRONG_BLOCK_SEQUENCE_COUNTER = 0x73
    RESPONSE_PENDING = 0x78
    SUB_FUNCTION_NOT_SUPPORTED_IN_SESSION = 0x7E
    SERVICE_NOT_SUPPORTED_IN_SESSION = 0x7F

    # ── descriptions ──────────────────────────────────────────────────────

    _DESCRIPTIONS: dict[int, str]  # declared for type checkers; set below

    @property
    def description(self) -> str:
        """Return a human-readable description of this NRC."""
        return _NRC_DESCRIPTIONS.get(self.value, "Unknown NRC")

    # ── constructors ──────────────────────────────────────────────────────

    @classmethod
    def from_byte(cls, b: int) -> UdsNrc:
        """Return the NRC enum member for *b*, or raise :class:`ValueError`."""
        try:
            return cls(b)
        except ValueError:
            raise ValueError(f"Unknown NRC byte: 0x{b:02X}") from None

    # ── dunder ────────────────────────────────────────────────────────────

    def __str__(self) -> str:
        return f"{self.name}(0x{self.value:02X}): {self.description}"


_NRC_DESCRIPTIONS: dict[int, str] = {
    0x10: "General reject — no more specific NRC applies",
    0x11: "Service ID is not supported by this ECU",
    0x12: "Sub-function byte is not supported for the requested service",
    0x13: "Message length or format is incorrect",
    0x14: "Response would exceed the maximum allowed length",
    0x21: "Server is busy; client should repeat the request later",
    0x22: "Conditions are not correct for the requested action",
    0x24: "Request sequence is incorrect (e.g., key before seed)",
    0x25: "No response from sub-net component",
    0x26: "A failure prevents execution of the requested action",
    0x31: "Request parameter is out of range",
    0x33: "Security access is denied (insufficient privilege level)",
    0x35: "Security key sent by tester is invalid",
    0x36: "Number of allowed security access attempts has been exceeded",
    0x37: "Required time delay has not yet expired",
    0x70: "Upload/download not accepted",
    0x71: "Transfer data has been suspended",
    0x72: "General programming failure (flash write/erase error)",
    0x73: "Block sequence counter in Transfer Data is wrong",
    0x78: "Request correctly received; response is pending",
    0x7E: "Sub-function is not supported in the currently active session",
    0x7F: "Service is not supported in the currently active session",
}
