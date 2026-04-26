# Development Log

This file records the development progress of the UDS Diagnostic Simulation System.
It is updated at the end of each development session so the next session can pick up
from where work left off.

---

## Session 1 — 2026-04-26

### Work Completed

| Area | Status | Notes |
|------|--------|-------|
| Git repo & CI/CD | ✅ Done | `ci.yml` + `release.yml` workflows verified passing |
| CMakeLists.txt (root + src + tests) | ✅ Done | C17, `-Wall -Wextra -Wpedantic`, coverage & ASAN options |
| `src/can/` — SocketCAN adapter (`uds_can`) | ✅ Done | Full implementation + 30+ unit tests (check framework) |
| `src/include/uds_can.h` | ✅ Done | Public header with error codes, filter helpers, CAN-ID helpers |
| `tests/unit/test_can_socket.c` | ✅ Done | Pure-logic + vCAN integration tests |
| `src/tp/` — ISO-TP transport layer (`uds_tp`) | ✅ Done | See session details below |
| `src/include/uds_tp.h` | ✅ Done | Frame encode/decode, high-level send/recv, RX channel |
| `tests/unit/test_iso_tp.c` | ✅ Done | 40+ unit tests covering encode/decode + vCAN integration |

### Phase 1 Milestone Progress

- **M1 (Week 1-2):** ✅ Complete — repo, CI/CD, CMake workspace
- **M1 (Week 2-3):** ✅ Complete — `uds_can` SocketCAN wrapper
- **M1 (Week 3-4):** ✅ Complete — `uds_tp` ISO-TP transport layer

### ISO-TP Implementation Summary (`uds_tp`)

Implemented in `src/tp/iso_tp.c` per ISO 15765-2 for standard 8-byte CAN frames:

**Frame encode functions** (pure, no I/O):
- `uds_tp_encode_sf()` — Single Frame (1–7 bytes)
- `uds_tp_encode_ff()` — First Frame (8–4095 bytes)
- `uds_tp_encode_cf()` — Consecutive Frame (sequence number 0–15)
- `uds_tp_encode_fc()` — Flow Control (CTS / Wait / Overflow)

**Frame decode functions** (pure, no I/O):
- `uds_tp_frame_type()` — classify frame from upper PCI nibble
- `uds_tp_decode_sf()` — extract payload from SF
- `uds_tp_decode_ff()` — extract total length + first 6 bytes from FF
- `uds_tp_decode_cf()` — extract sequence number + payload chunk from CF
- `uds_tp_decode_fc()` — extract flow status, block size, STmin from FC

**High-level I/O functions**:
- `uds_tp_send()` — send a complete PDU (auto SF or FF+CF+FC handling)
- `uds_tp_recv()` — receive a complete PDU (auto SF or FF+CF+FC reassembly)
- `uds_tp_rx_init()` — initialise a `UdsTpRxChannel` state machine

**Key design decisions**:
- No dynamic memory allocation; all buffers caller-supplied
- Caller is responsible for setting CAN socket filters before calling send/recv
- `UdsTpConfig` holds `timeout_ms`, `block_size`, `st_min` per-transfer
- STmin delay is applied between CF frames (usleep, ms + µs ranges per standard)
- Max Wait Frame count guard (`UDS_TP_MAX_WAIT_FRAMES = 10`)

---

## Next Session — Recommended Work (Phase 2)

### Priority: Phase 2 Week 5-6 — Session & Security Services (`uds_core`)

The `uds_can` and `uds_tp` layers are complete. The next logical step is to implement
the UDS core diagnostic services layer.

**Recommended parallel workstreams:**

1. **`src/core/session.c`** — Service 0x10 (Diagnostic Session Control)
   - Default / Programming / Extended / Security sessions
   - Session timeout management (P2, P2*, S3 timers)
   - Per-ECU session state isolation

2. **`src/core/tester_present.c`** — Service 0x3E (Tester Present)
   - Suppress positive response sub-function
   - Reset S3 session keepalive timer

3. **`src/core/security_access.c`** — Service 0x27 (Security Access)
   - Seed generation and key verification (XOR algorithm)
   - Failed attempt lock-out (3 attempts → lock with cooldown)
   - NRC mapping: `0x35` (invalidKey), `0x36` (exceededNumberOfAttempts),
     `0x37` (requiredTimeDelayNotExpired)

4. **`src/include/uds_core.h`** — unified header for core services

5. **`tests/unit/test_session.c`** and **`tests/unit/test_security.c`**

**Also needed:**
- `src/include/uds_nrc.h` — NRC (Negative Response Code) enum covering all
  ISO 14229-1 NRCs used by core services

### Files to Create

```
src/
  include/
    uds_nrc.h        ← NRC enum (new)
    uds_core.h       ← core service API (new)
  core/
    CMakeLists.txt   ← builds uds_core static lib (new)
    session.c        ← 0x10 + 0x3E session management (new)
    security.c       ← 0x27 security access (new)
tests/unit/
  test_session.c     ← unit tests for session layer (new)
  test_security.c    ← unit tests for security access (new)
```

### Files to Update

```
src/CMakeLists.txt         ← add_subdirectory(core)
tests/unit/CMakeLists.txt  ← add test_session and test_security targets
```

---

## Architecture Reference

```
src/
  include/        ← public headers (uds_can.h, uds_tp.h, uds_core.h, …)
  can/            ← SocketCAN adapter (DONE)
  tp/             ← ISO-TP transport (DONE)
  core/           ← UDS diagnostic services (TODO Phase 2)
  bootloader/     ← Bootloader & flash sim (TODO Phase 3)
  ecusim/         ← ECU simulator (TODO Phase 3)
tools/            ← Python tools (TODO Phase 4)
tests/
  unit/           ← C unit tests (check framework)
  integration/    ← Python integration tests (vCAN)
```
