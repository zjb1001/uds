# ── src/CMakeLists.txt ──────────────────────────────────────────────────────
# Top-level source subdirectory.  Add new modules here as they are developed.

# CAN adapter layer (Phase 1 — SocketCAN wrapper)
add_subdirectory(can)

# ISO-TP transport layer (Phase 1 — ISO 15765-2)
add_subdirectory(tp)

# UDS core diagnostic services (Phase 2 — 0x10 / 0x3E / 0x27)
add_subdirectory(core)

# UDS bootloader and flash programming services (Phase 3 — 0x34/0x35/0x36/0x37)
add_subdirectory(bootloader)
