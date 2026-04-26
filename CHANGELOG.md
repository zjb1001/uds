# Changelog

All notable changes to the UDS Diagnostic Simulation System are documented in
this file.  The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [1.0.0] — 2026-04-26

### Fixed

- **`security.c`**: Expired seed now returns NRC `0x24` (requestSequenceError)
  instead of the incorrect `0x37` (requiredTimeDelayNotExpired).  NRC `0x37`
  is reserved for the lockout-cooldown path; an expired seed means sendKey
  arrives out-of-sequence.
- **`session.c`**: DSC positive response (0x50) now encodes P2* in units of
  **10 ms** as required by ISO 14229-1 §7.4.3.2, instead of raw milliseconds.
  Default `p2_star_ms = 2000 ms` is now transmitted as `0x00C8` (200 × 10 ms).
- **`flash_services.c` / `uds_flash.h`**: `addressAndLengthFormatIdentifier`
  byte now parsed per ISO 14229-1: **high nibble = memorySize length,
  low nibble = memoryAddress length**.  The previous implementation had the
  nibbles swapped, causing wrong address/size extraction whenever the two
  field widths differ.
- **`ecusim.c`** (0x34 / 0x35 dispatch): Request bytes now correctly indexed:
  `req[2]` is the `addressAndLengthFormatIdentifier`; address+size data starts
  at `req[3]`.  Previously `req[1]` (the `dataFormatIdentifier` byte, always
  `0x00`) was used as the format, causing every download/upload request to fail
  with NRC `0x13`.
- **`tools/uds/flash.py`**: Block sequence counter now wraps `0xFF → 0x01` per
  ISO 14229-1 (never `0x00`).
- **`tools/uds/ecusim.py`**:
  - Wrong block sequence counter in 0x36 now returns NRC `0x73`
    (wrongBlockSequenceCounter) instead of `0x24` (requestSequenceError).
  - Block sequence counter wrap corrected to `0xFF → 0x01` (two sites).
  - DSC response now encodes P2* in 10 ms units (`0x00C8`) to match
    ISO 14229-1 and the C server.

### Added

#### Phase 1 — Foundation
- **`src/can/`** — SocketCAN adapter library (`uds_can`).
  Full implementation with CAN ID helpers, filter helpers, and error codes.
  39 unit tests.
- **`src/tp/`** — ISO-TP transport layer (`uds_tp`) per ISO 15765-2.
  Encode/decode for SF / FF / CF / FC; high-level `uds_tp_send()` /
  `uds_tp_recv()`.  55 unit tests.
- **`src/include/uds_can.h`** and **`src/include/uds_tp.h`** — public C headers.
- CMake workspace with C17, `-Wall -Wextra -Wpedantic`, ASAN and lcov coverage.
- GitHub Actions CI (`ci.yml`) with lint, C build+test, Python unit tests, and
  vCAN integration test jobs.
- GitHub Actions release workflow (`release.yml`).

#### Phase 2 — Core UDS Services
- **`src/core/`** — UDS core diagnostic services library (`uds_core`):
  - `session.c` — Service 0x10 (DSC) + 0x3E (Tester Present) with P2/P2*/S3
    timer management.
  - `security.c` — Service 0x27 (Security Access) with XOR seed/key, 3-attempt
    lock-out, and NRC mapping.
  - `data.c` — Services 0x22 / 0x2E / 0x11 / 0x28 (Read/Write DID, ECU Reset,
    Comm Control).
  - `dtc.c` — Services 0x14 / 0x19 (Clear DTC, Read DTC sub-fn 0x01/0x02/0x0A).
  - `routine_control.c` — Service 0x31 (Routine Control).
  - `nrc.c` — NRC helper functions.
- **`src/include/uds_core.h`**, `uds_nrc.h`, `uds_data.h`, `uds_dtc.h`,
  `uds_routine.h` — public C headers.
- Unit tests: `test_session.c`, `test_security.c`, `test_data.c`, `test_dtc.c`
  (≈ 110 tests total across four suites).

#### Phase 3 — Flash Programming
- **`src/bootloader/flash.c`** — Flash memory simulation (init, erase, write,
  read, address validation; 256 KB region at 0x00000000).
- **`src/bootloader/flash_services.c`** — UDS services 0x34 / 0x35 / 0x36 / 0x37
  (RequestDownload / RequestUpload / TransferData / RequestTransferExit).
- **`src/include/uds_flash.h`** — public C header.
- Unit tests: `test_flash.c` (7 suites, ≈ 30 tests).

#### Phase 4 — Python Tools
- **`tools/uds/`** — Python UDS library package:
  - `nrc.py` — `UdsNrc` IntEnum covering all ISO 14229-1 NRC codes.
  - `exceptions.py` — `UdsError`, `UdsNrcError`, `UdsTimeoutError`,
    `UdsProtocolError`.
  - `transport.py` — `IsoTpTransport` (SF/FF/CF/FC framing over python-can).
  - `client.py` — `UdsClient` with 0x78 response-pending retry logic.
  - `session.py` — Services 0x10 / 0x3E.
  - `security.py` — Service 0x27 (XOR key).
  - `data.py` — Services 0x22 / 0x2E / 0x11 / 0x28.
  - `dtc.py` — Services 0x14 / 0x19 (sub-fn 0x01/0x02/0x0A).
  - `flash.py` — Services 0x34 / 0x35 / 0x36 / 0x37 with block sequencing and
    progress callback.
- **`tools/diag_cli.py`** — Click-based diagnostic CLI (session, tester-present,
  read-did, write-did, read-dtc, clear-dtc, reset, unlock).
- **`tools/flash_tool.py`** — Click-based flash CLI (download, upload) with Rich
  progress bars.
- Python unit tests: `test_uds_nrc.py`, `test_uds_security.py`,
  `test_uds_session.py`, `test_uds_data.py` — 94 tests total.

#### Phase 5 — ECU Simulator & Integration Tests
- **`src/ecusim/`** — C ECU simulator executable supporting services
  0x10 / 0x3E / 0x27 / 0x22 / 0x2E / 0x11 / 0x28 / 0x14 / 0x19 /
  0x34 / 0x35 / 0x36 / 0x37.  CLI: `--interface`, `--ecu-id`, `--verbose`.
- **`tools/uds/ecusim.py`** — Python `EcuSimulator` class for use in integration
  tests; supports all core UDS services plus flash services (0x34/0x35/0x36/0x37)
  with a 256 KB simulated flash region.
- **`tests/integration/conftest.py`** — vCAN fixtures: `skip_if_no_vcan`,
  `running_ecusim`, `uds_client_with_sim`.
- **`tests/integration/test_session_integration.py`** — ISO-TP transport lifecycle
  tests.
- **`tests/integration/test_uds_services_integration.py`** — 26 integration tests
  covering services 0x10 / 0x3E / 0x27 / 0x22 / 0x2E / 0x11 / 0x19 / 0x14.
- **`tests/integration/test_multi_ecu.py`** — Multi-ECU concurrency tests:
  session isolation, security state isolation, DID data isolation, concurrent
  requests (10 tests).
- **`tests/integration/test_flash_performance.py`** — Flash throughput benchmark
  and protocol correctness tests (8 tests); validates ≥ 10 KB/s download/upload
  target.

### C Test Suite Summary (all pass)

| Suite | Tests |
|-------|-------|
| uds_can_unit_tests | 39 |
| uds_tp_unit_tests | 55 |
| uds_core_session_unit_tests | 30 |
| uds_core_security_unit_tests | 30 |
| uds_data_unit_tests | 25 |
| uds_dtc_unit_tests | 25 |
| uds_flash_unit_tests | 30 |
| **Total** | **234** |

### Python Test Suite Summary

| Module | Tests |
|--------|-------|
| test_uds_nrc.py | 15 |
| test_uds_security.py | 25 |
| test_uds_session.py | 29 |
| test_uds_data.py | 25 |
| **Unit total** | **94** |
| test_session_integration.py | 4 (vCAN) |
| test_uds_services_integration.py | 26 (vCAN) |
| test_multi_ecu.py | 10 (vCAN) |
| test_flash_performance.py | 8 (vCAN) |
| **Integration total** | **48** |

---

## [Unreleased]

### Planned
- CAN FD (ISO 15765-3) support
- DoIP (ISO 13400-2) transport
- Graphical diagnostic dashboard
- v1.1.0 — Performance optimisations and additional ECU types

[1.0.0]: https://github.com/zjb1001/uds/releases/tag/v1.0.0
