# ── src/CMakeLists.txt ──────────────────────────────────────────────────────
# Top-level source subdirectory.  Add new modules here as they are developed.

# CAN adapter layer (Phase 1 — SocketCAN wrapper)
add_subdirectory(can)

# ISO-TP transport layer (Phase 1 — ISO 15765-2)
add_subdirectory(tp)
