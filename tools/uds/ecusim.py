"""Python ECU simulator for integration testing.

Implements a minimal UDS server over raw SocketCAN / ISO-TP.
Designed to be launched as a background thread in integration tests so that
``UdsClient`` instances have a real ECU to talk to without needing to run the
C ``ecusim`` binary.

Supported services
------------------
- 0x10  Diagnostic Session Control
- 0x3E  Tester Present
- 0x27  Security Access  (XOR seed/key, mask = [0xAB, 0xCD, 0x12, 0x34])
- 0x22  ReadDataByIdentifier
- 0x2E  WriteDataByIdentifier
- 0x11  ECUReset
- 0x14  ClearDiagnosticInformation
- 0x19  ReadDTCInformation (sub-fn 0x01 / 0x02 / 0x0A)
- 0x34  RequestDownload
- 0x35  RequestUpload
- 0x36  TransferData
- 0x37  RequestTransferExit
"""

from __future__ import annotations

import contextlib
import os
import socket
import struct
import threading
import time
from typing import ClassVar

# ── CAN frame layout ──────────────────────────────────────────────────────
# struct can_frame { __u32 can_id; __u8 can_dlc; __u8 pad[3]; __u8 data[8]; }
_CAN_FRAME_FMT = "=IB3x8s"
_CAN_FRAME_SIZE = struct.calcsize(_CAN_FRAME_FMT)
_EFF_FLAG = 0x80000000  # Extended frame flag (must NOT be set for 11-bit IDs)
_RTR_FLAG = 0x40000000
_ERR_FLAG = 0x20000000

# ── ISO-TP frame type nibbles ──────────────────────────────────────────────
_SF = 0x0  # Single Frame
_FF = 0x1  # First Frame
_CF = 0x2  # Consecutive Frame
_FC = 0x3  # Flow Control

# ── UDS NRC bytes ─────────────────────────────────────────────────────────
_NRC_GENERAL_REJECT = 0x10
_NRC_SERVICE_NOT_SUPPORTED = 0x11
_NRC_SUB_FUNCTION_NOT_SUPPORTED = 0x12
_NRC_INCORRECT_MSG_LEN = 0x13
_NRC_REQUEST_OUT_OF_RANGE = 0x31
_NRC_SECURITY_ACCESS_DENIED = 0x33
_NRC_INVALID_KEY = 0x35
_NRC_EXCEEDED_ATTEMPTS = 0x36
_NRC_TIME_DELAY_NOT_EXPIRED = 0x37
_NRC_CONDITIONS_NOT_CORRECT = 0x22
_NRC_REQUEST_SEQUENCE_ERROR = 0x24
_NRC_WRONG_BLOCK_SEQUENCE_COUNTER = 0x73

# ── Security XOR mask (must match C implementation) ───────────────────────
_SECURITY_MASK = bytes([0xAB, 0xCD, 0x12, 0x34])
_SEED_LEN = 4
_MAX_ATTEMPTS = 3
_LOCKOUT_SECS = 300

# ── Flash simulation ───────────────────────────────────────────────────────
_FLASH_BASE = 0x00000000
_FLASH_SIZE = 256 * 1024  # 256 KB simulated flash region
_FLASH_BLOCK_SIZE = 256  # max data bytes per TransferData request
# maxBlockLength reported to client: block_size + 1 (sequence number byte)
_FLASH_MAX_BLOCK_LEN = _FLASH_BLOCK_SIZE + 1


def _compute_key(seed: bytes) -> bytes:
    """Compute expected key from seed using XOR mask."""
    return bytes(seed[i] ^ _SECURITY_MASK[i % 4] for i in range(len(seed)))


class EcuSimulator:
    """A simple UDS ECU simulator that listens on a SocketCAN interface.

    Parameters
    ----------
    channel:
        CAN interface name (e.g. ``"vcan0"``).
    ecu_id:
        Logical ECU identifier.  CAN IDs: rx = 0x600+ecu_id, tx = 0x680+ecu_id.
    timeout:
        Socket receive timeout in seconds (controls loop check frequency).
    """

    # Default DID table  {did_id: {"data": bytearray, "writable": bool,
    #                               "min_write_session": int}}
    DEFAULT_DIDS: ClassVar[dict[int, dict]] = {
        0xF187: {
            "data": bytearray(b"UDS-SIM-0001"),
            "writable": True,
            "min_write_session": 0x03,
        },
        0xF191: {
            "data": bytearray(b"HW-1.0"),
            "writable": False,
            "min_write_session": 0x03,
        },
        0xF189: {
            "data": bytearray(b"SW-1.0.0"),
            "writable": False,
            "min_write_session": 0x03,
        },
        0xF190: {
            "data": bytearray(b"1HGCM82633A123456"),
            "writable": True,
            "min_write_session": 0x03,
        },
        0x0101: {
            "data": bytearray([0x00, 0x00, 0x12, 0x34]),
            "writable": True,
            "min_write_session": 0x03,
        },
    }

    # Default DTC table  {dtc_code: status_byte}
    DEFAULT_DTCS: ClassVar[dict[int, int]] = {
        0x010001: 0x28,  # confirmed + test_failed_since_clear
        0x010002: 0x04,  # pending
    }

    def __init__(self, channel: str, ecu_id: int, timeout: float = 0.1) -> None:
        self._channel = channel
        self._ecu_id = ecu_id
        self._rx_id = 0x600 + ecu_id
        self._tx_id = 0x680 + ecu_id
        self._timeout = timeout
        self._sock: socket.socket | None = None
        self._thread: threading.Thread | None = None
        self._running = False

        # UDS state
        self._session = 0x01  # defaultSession
        self._session_ts = time.monotonic()
        self._security_level = 0  # 0 = not unlocked
        self._pending_seed: bytes | None = None
        self._pending_level = 0
        self._fail_count = 0
        self._locked = False
        self._locked_ts = 0.0

        # DID table (copy defaults so each instance is independent)
        self._dids: dict[int, dict] = {
            k: {
                "data": bytearray(v["data"]),
                "writable": v["writable"],
                "min_write_session": v["min_write_session"],
            }
            for k, v in self.DEFAULT_DIDS.items()
        }

        # DTC table
        self._dtcs: dict[int, int] = dict(self.DEFAULT_DTCS)

        # Flash memory simulation
        self._flash: bytearray = bytearray(_FLASH_SIZE)
        # Transfer session state
        self._xfer_mode: int = 0  # 0=idle, 0x34=download, 0x35=upload
        self._xfer_address: int = 0
        self._xfer_size: int = 0
        self._xfer_offset: int = 0
        self._xfer_expected_sn: int = 1

    # ── Lifecycle ─────────────────────────────────────────────────────────

    def open(self) -> None:
        """Open the CAN socket and bind to the interface."""
        if self._sock is not None:
            return
        sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
        sock.settimeout(self._timeout)
        sock.bind((self._channel,))

        # Apply CAN ID filter: receive only the request CAN ID
        can_id = self._rx_id & 0x7FF  # 11-bit
        can_mask = 0x7FF
        filt = struct.pack("=II", can_id, can_mask)
        sock.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_FILTER, filt)

        self._sock = sock

    def close(self) -> None:
        """Close the CAN socket."""
        if self._sock is not None:
            self._sock.close()
            self._sock = None

    def start(self) -> None:
        """Open socket and start background receive loop thread."""
        self.open()
        self._running = True
        self._thread = threading.Thread(
            target=self._run_loop, daemon=True, name=f"ecusim-{self._ecu_id}"
        )
        self._thread.start()

    def stop(self, timeout: float = 2.0) -> None:
        """Stop the background thread and close socket."""
        self._running = False
        if self._thread is not None:
            self._thread.join(timeout=timeout)
            self._thread = None
        self.close()

    def __enter__(self) -> EcuSimulator:
        self.start()
        return self

    def __exit__(self, *_: object) -> None:
        self.stop()

    # ── Receive / send helpers ────────────────────────────────────────────

    def _send_frame(self, data: bytes) -> None:
        """Send one raw 8-byte CAN frame from this ECU (tx_id)."""
        if self._sock is None:
            return
        payload = bytes(data).ljust(8, b"\x00")[:8]
        frame = struct.pack(_CAN_FRAME_FMT, self._tx_id, 8, payload)
        with contextlib.suppress(OSError):
            self._sock.send(frame)

    def _send_isotp(self, pdu: bytes) -> None:
        """Send a complete UDS PDU using ISO-TP framing."""
        if len(pdu) <= 7:
            # Single Frame
            sf = bytes([(_SF << 4) | len(pdu)]) + pdu
            self._send_frame(sf)
        else:
            # First Frame
            total = len(pdu)
            ff = bytes([(_FF << 4) | ((total >> 8) & 0x0F), total & 0xFF]) + pdu[:6]
            self._send_frame(ff)

            # Wait for Flow Control from tester
            fc = self._recv_raw_frame(timeout=1.0)
            if fc is None or (fc[0] >> 4) != _FC:
                return  # timeout or unexpected frame
            # fc[1] = block_size, fc[2] = st_min (ignored in simulation)

            # Consecutive Frames
            sn = 1
            offset = 6
            while offset < total:
                chunk = pdu[offset : offset + 7]
                cf = bytes([(_CF << 4) | (sn & 0x0F)]) + chunk
                self._send_frame(cf)
                offset += len(chunk)
                sn = (sn + 1) & 0x0F

    def _recv_raw_frame(self, timeout: float | None = None) -> bytes | None:
        """Receive one CAN frame payload (8 bytes) with the expected rx_id."""
        if self._sock is None:
            return None
        deadline = time.monotonic() + (timeout if timeout is not None else self._timeout)
        while time.monotonic() < deadline:
            try:
                raw = self._sock.recv(_CAN_FRAME_SIZE)
            except TimeoutError:
                return None
            except OSError:
                return None
            if len(raw) < _CAN_FRAME_SIZE:
                continue
            can_id, dlc, data = struct.unpack(_CAN_FRAME_FMT, raw)
            # Strip EFF/RTR/ERR flags; use 0x7FF mask for standard 11-bit IDs
            actual_id = can_id & 0x7FF
            if actual_id == self._rx_id:
                return data[:dlc]
        return None

    def _recv_isotp(self, timeout: float = 2.0) -> bytes | None:
        """Receive one complete ISO-TP PDU."""
        deadline = time.monotonic() + timeout

        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            frame = self._recv_raw_frame(timeout=min(remaining, 0.1))
            if frame is None:
                continue

            frame_type = (frame[0] >> 4) & 0x0F

            if frame_type == _SF:
                length = frame[0] & 0x0F
                return bytes(frame[1 : 1 + length])

            if frame_type == _FF:
                total = ((frame[0] & 0x0F) << 8) | frame[1]
                buf = bytearray(frame[2:8])

                # Send Flow Control CTS
                fc = bytes([0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
                self._send_frame(fc)

                expected_sn = 1
                while len(buf) < total:
                    remaining_inner = deadline - time.monotonic()
                    if remaining_inner <= 0:
                        return None
                    cf = self._recv_raw_frame(timeout=min(remaining_inner, 0.5))
                    if cf is None:
                        return None
                    if (cf[0] >> 4) != _CF:
                        return None
                    sn = cf[0] & 0x0F
                    if sn != expected_sn:
                        return None
                    remaining_bytes = total - len(buf)
                    buf += cf[1 : 1 + min(7, remaining_bytes)]
                    expected_sn = (expected_sn + 1) & 0x0F

                return bytes(buf[:total])

        return None

    # ── Main loop ─────────────────────────────────────────────────────────

    def _run_loop(self) -> None:
        """Background thread: receive UDS requests and send responses."""
        while self._running:
            pdu = self._recv_isotp(timeout=0.1)
            if pdu is None:
                continue
            resp = self._dispatch(pdu)
            if resp is not None and len(resp) > 0:
                self._send_isotp(resp)

    # ── UDS dispatcher ────────────────────────────────────────────────────

    def _nrc(self, sid: int, nrc: int) -> bytes:
        return bytes([0x7F, sid, nrc])

    def _dispatch(self, req: bytes) -> bytes | None:
        if not req:
            return None
        sid = req[0]

        handlers = {
            0x10: self._svc_dsc,
            0x3E: self._svc_tester_present,
            0x27: self._svc_security_access,
            0x22: self._svc_read_did,
            0x2E: self._svc_write_did,
            0x11: self._svc_ecu_reset,
            0x14: self._svc_clear_dtc,
            0x19: self._svc_read_dtc,
            0x34: self._svc_request_download,
            0x35: self._svc_request_upload,
            0x36: self._svc_transfer_data,
            0x37: self._svc_transfer_exit,
        }

        handler = handlers.get(sid)
        if handler is None:
            return self._nrc(sid, _NRC_SERVICE_NOT_SUPPORTED)
        return handler(req)

    # ── Service 0x10: Diagnostic Session Control ──────────────────────────

    def _svc_dsc(self, req: bytes) -> bytes:
        sid = 0x10
        if len(req) < 2:
            return self._nrc(sid, _NRC_INCORRECT_MSG_LEN)
        session_type = req[1]
        if session_type not in (0x01, 0x02, 0x03, 0x04):
            return self._nrc(sid, _NRC_SUB_FUNCTION_NOT_SUPPORTED)
        self._session = session_type
        self._session_ts = time.monotonic()
        if session_type == 0x01:
            # Return to default session: clear security
            self._security_level = 0
            self._pending_seed = None
        # Response: [0x50, sessionType, P2_hi, P2_lo, P2star_hi, P2star_lo]
        # P2* is in units of 10 ms per ISO 14229-1 (2000 ms / 10 = 200 = 0x00C8)
        return bytes([0x50, session_type, 0x00, 0x32, 0x00, 0xC8])

    # ── Service 0x3E: Tester Present ──────────────────────────────────────

    def _svc_tester_present(self, req: bytes) -> bytes | None:
        sid = 0x3E
        if len(req) < 2:
            return self._nrc(sid, _NRC_INCORRECT_MSG_LEN)
        sub_fn = req[1]
        suppress = (sub_fn & 0x80) != 0
        actual = sub_fn & 0x7F
        if actual != 0x00:
            return self._nrc(sid, _NRC_SUB_FUNCTION_NOT_SUPPORTED)
        self._session_ts = time.monotonic()
        if suppress:
            return None
        return bytes([0x7E, 0x00])

    # ── Service 0x27: Security Access ─────────────────────────────────────

    def _svc_security_access(self, req: bytes) -> bytes:
        sid = 0x27
        if len(req) < 2:
            return self._nrc(sid, _NRC_INCORRECT_MSG_LEN)
        sub_fn = req[1]

        if sub_fn & 0x01:  # odd → requestSeed
            level = sub_fn
            if self._locked:
                elapsed = time.monotonic() - self._locked_ts
                if elapsed < _LOCKOUT_SECS:
                    return self._nrc(sid, _NRC_TIME_DELAY_NOT_EXPIRED)
                # Lockout expired
                self._locked = False
                self._fail_count = 0

            if self._security_level == level:
                # Already unlocked: return zero seed
                return bytes([0x67, sub_fn, 0x00, 0x00, 0x00, 0x00])

            # Generate seed using os.urandom
            seed = os.urandom(_SEED_LEN)
            self._pending_seed = seed
            self._pending_level = level
            return bytes([0x67, sub_fn]) + seed

        else:  # even → sendKey
            level = sub_fn - 1  # corresponding odd level
            if self._pending_seed is None or self._pending_level != level:
                return self._nrc(sid, _NRC_REQUEST_SEQUENCE_ERROR)

            if len(req) < 2 + _SEED_LEN:
                return self._nrc(sid, _NRC_INCORRECT_MSG_LEN)

            key_received = req[2 : 2 + _SEED_LEN]
            key_expected = _compute_key(self._pending_seed)
            self._pending_seed = None

            if bytes(key_received) != key_expected:
                self._fail_count += 1
                if self._fail_count >= _MAX_ATTEMPTS:
                    self._locked = True
                    self._locked_ts = time.monotonic()
                    return self._nrc(sid, _NRC_EXCEEDED_ATTEMPTS)
                return self._nrc(sid, _NRC_INVALID_KEY)

            # Key correct
            self._security_level = level
            self._fail_count = 0
            return bytes([0x67, sub_fn])

    # ── Service 0x22: ReadDataByIdentifier ────────────────────────────────

    def _svc_read_did(self, req: bytes) -> bytes:
        sid = 0x22
        if len(req) < 3 or (len(req) - 1) % 2 != 0:
            return self._nrc(sid, _NRC_INCORRECT_MSG_LEN)

        result = bytearray([0x62])
        did_bytes = req[1:]
        for i in range(0, len(did_bytes), 2):
            did_id = (did_bytes[i] << 8) | did_bytes[i + 1]
            entry = self._dids.get(did_id)
            if entry is None:
                return self._nrc(sid, _NRC_REQUEST_OUT_OF_RANGE)
            result += bytes([did_id >> 8, did_id & 0xFF]) + bytes(entry["data"])
        return bytes(result)

    # ── Service 0x2E: WriteDataByIdentifier ───────────────────────────────

    def _svc_write_did(self, req: bytes) -> bytes:
        sid = 0x2E
        if len(req) < 4:
            return self._nrc(sid, _NRC_INCORRECT_MSG_LEN)
        did_id = (req[1] << 8) | req[2]
        data = req[3:]
        entry = self._dids.get(did_id)
        if entry is None or not entry["writable"]:
            return self._nrc(sid, _NRC_REQUEST_OUT_OF_RANGE)
        if self._session < entry["min_write_session"]:
            return self._nrc(sid, _NRC_REQUEST_OUT_OF_RANGE)
        if len(data) != len(entry["data"]):
            return self._nrc(sid, _NRC_INCORRECT_MSG_LEN)
        entry["data"][:] = data
        return bytes([0x6E, req[1], req[2]])

    # ── Service 0x11: ECUReset ────────────────────────────────────────────

    def _svc_ecu_reset(self, req: bytes) -> bytes:
        sid = 0x11
        if len(req) < 2:
            return self._nrc(sid, _NRC_INCORRECT_MSG_LEN)
        reset_type = req[1]
        if reset_type not in (0x01, 0x02, 0x03):
            return self._nrc(sid, _NRC_SUB_FUNCTION_NOT_SUPPORTED)
        # Reset state
        self._session = 0x01
        self._security_level = 0
        self._pending_seed = None
        return bytes([0x51, reset_type])

    # ── Service 0x14: ClearDiagnosticInformation ─────────────────────────

    def _svc_clear_dtc(self, req: bytes) -> bytes:
        sid = 0x14
        if len(req) < 4:
            return self._nrc(sid, _NRC_INCORRECT_MSG_LEN)
        group = (req[1] << 16) | (req[2] << 8) | req[3]
        if group == 0xFFFFFF:
            # Clear all DTCs
            for code in self._dtcs:
                self._dtcs[code] = 0x00
        else:
            high_byte = (group >> 16) & 0xFF
            matched = False
            for code in self._dtcs:
                if (code >> 16) & 0xFF == high_byte:
                    self._dtcs[code] = 0x00
                    matched = True
            if not matched:
                return self._nrc(sid, _NRC_REQUEST_OUT_OF_RANGE)
        return bytes([0x54])

    # ── Service 0x19: ReadDTCInformation ──────────────────────────────────

    def _svc_read_dtc(self, req: bytes) -> bytes:
        sid = 0x19
        if len(req) < 2:
            return self._nrc(sid, _NRC_INCORRECT_MSG_LEN)
        sub_fn = req[1]

        if sub_fn == 0x01:  # reportNumberOfDTCByStatusMask
            if len(req) < 3:
                return self._nrc(sid, _NRC_INCORRECT_MSG_LEN)
            mask = req[2]
            count = sum(1 for s in self._dtcs.values() if s & mask)
            # Response: [0x59, 0x01, 0xFF, 0x01, count_hi, count_lo]
            return bytes([0x59, 0x01, 0xFF, 0x01, count >> 8, count & 0xFF])

        if sub_fn == 0x02:  # reportDTCByStatusMask
            if len(req) < 3:
                return self._nrc(sid, _NRC_INCORRECT_MSG_LEN)
            mask = req[2]
            result = bytearray([0x59, 0x02, mask])
            for code, status in self._dtcs.items():
                if status & mask:
                    result += bytes([(code >> 16) & 0xFF, (code >> 8) & 0xFF, code & 0xFF, status])
            return bytes(result)

        if sub_fn == 0x0A:  # reportSupportedDTC
            result = bytearray([0x59, 0x0A, 0xFF])
            for code, status in self._dtcs.items():
                result += bytes([(code >> 16) & 0xFF, (code >> 8) & 0xFF, code & 0xFF, status])
            return bytes(result)

        return self._nrc(sid, _NRC_SUB_FUNCTION_NOT_SUPPORTED)

    # ── Service 0x34: RequestDownload ─────────────────────────────────────

    def _svc_request_download(self, req: bytes) -> bytes:
        sid = 0x34
        # Minimum: [0x34, dataFormatIdentifier, addressAndLengthFormatIdentifier,
        #           address bytes..., size bytes...]
        if len(req) < 4:
            return self._nrc(sid, _NRC_INCORRECT_MSG_LEN)

        addr_len_fmt = req[2]
        addr_len = addr_len_fmt & 0x0F
        size_len = (addr_len_fmt >> 4) & 0x0F
        if addr_len == 0 or size_len == 0:
            return self._nrc(sid, _NRC_REQUEST_OUT_OF_RANGE)
        if len(req) < 3 + addr_len + size_len:
            return self._nrc(sid, _NRC_INCORRECT_MSG_LEN)

        address = int.from_bytes(req[3 : 3 + addr_len], "big")
        size = int.from_bytes(req[3 + addr_len : 3 + addr_len + size_len], "big")

        if address < _FLASH_BASE or address + size > _FLASH_BASE + _FLASH_SIZE:
            return self._nrc(sid, _NRC_REQUEST_OUT_OF_RANGE)

        # Abort any active transfer
        self._xfer_mode = 0x34
        self._xfer_address = address
        self._xfer_size = size
        self._xfer_offset = 0
        self._xfer_expected_sn = 1

        # Erase (zero) the target region
        start = address - _FLASH_BASE
        self._flash[start : start + size] = bytearray(size)

        # Response: [0x74, len_format_byte, max_block_len_hi, max_block_len_lo]
        # len_format = 0x20 → 2 bytes for maxBlockLength
        mbl = _FLASH_MAX_BLOCK_LEN
        return bytes([0x74, 0x20, (mbl >> 8) & 0xFF, mbl & 0xFF])

    # ── Service 0x35: RequestUpload ───────────────────────────────────────

    def _svc_request_upload(self, req: bytes) -> bytes:
        sid = 0x35
        if len(req) < 4:
            return self._nrc(sid, _NRC_INCORRECT_MSG_LEN)

        addr_len_fmt = req[2]
        addr_len = addr_len_fmt & 0x0F
        size_len = (addr_len_fmt >> 4) & 0x0F
        if addr_len == 0 or size_len == 0:
            return self._nrc(sid, _NRC_REQUEST_OUT_OF_RANGE)
        if len(req) < 3 + addr_len + size_len:
            return self._nrc(sid, _NRC_INCORRECT_MSG_LEN)

        address = int.from_bytes(req[3 : 3 + addr_len], "big")
        size = int.from_bytes(req[3 + addr_len : 3 + addr_len + size_len], "big")

        if address < _FLASH_BASE or address + size > _FLASH_BASE + _FLASH_SIZE:
            return self._nrc(sid, _NRC_REQUEST_OUT_OF_RANGE)

        self._xfer_mode = 0x35
        self._xfer_address = address
        self._xfer_size = size
        self._xfer_offset = 0
        self._xfer_expected_sn = 1

        mbl = _FLASH_MAX_BLOCK_LEN
        return bytes([0x75, 0x20, (mbl >> 8) & 0xFF, mbl & 0xFF])

    # ── Service 0x36: TransferData ────────────────────────────────────────

    def _svc_transfer_data(self, req: bytes) -> bytes:
        sid = 0x36
        if self._xfer_mode == 0:
            return self._nrc(sid, _NRC_REQUEST_SEQUENCE_ERROR)
        if len(req) < 2:
            return self._nrc(sid, _NRC_INCORRECT_MSG_LEN)

        block_sn = req[1]
        if block_sn != (self._xfer_expected_sn & 0xFF):
            return self._nrc(sid, _NRC_WRONG_BLOCK_SEQUENCE_COUNTER)

        if self._xfer_mode == 0x34:  # download: tester sends data
            data = req[2:]
            flash_start = self._xfer_address - _FLASH_BASE + self._xfer_offset
            remaining = self._xfer_size - self._xfer_offset
            chunk = data[:remaining]
            self._flash[flash_start : flash_start + len(chunk)] = chunk
            self._xfer_offset += len(chunk)
            # ISO 14229-1: block sequence counter wraps 0xFF → 0x01 (never 0x00)
            self._xfer_expected_sn = 1 if self._xfer_expected_sn >= 0xFF else self._xfer_expected_sn + 1
            return bytes([0x76, block_sn])

        # upload: ECU sends data
        flash_start = self._xfer_address - _FLASH_BASE + self._xfer_offset
        remaining = self._xfer_size - self._xfer_offset
        chunk_size = min(_FLASH_BLOCK_SIZE, remaining)
        chunk = bytes(self._flash[flash_start : flash_start + chunk_size])
        self._xfer_offset += chunk_size
        # ISO 14229-1: block sequence counter wraps 0xFF → 0x01 (never 0x00)
        self._xfer_expected_sn = 1 if self._xfer_expected_sn >= 0xFF else self._xfer_expected_sn + 1
        return bytes([0x76, block_sn]) + chunk

    # ── Service 0x37: RequestTransferExit ────────────────────────────────

    def _svc_transfer_exit(self, req: bytes) -> bytes:  # noqa: ARG002
        if self._xfer_mode == 0:
            return self._nrc(0x37, _NRC_REQUEST_SEQUENCE_ERROR)
        self._xfer_mode = 0
        self._xfer_offset = 0
        return bytes([0x77])
