# ── src/bootloader/CMakeLists.txt ────────────────────────────────────────────
# Builds the uds_bootloader static library (Phase 3 — flash simulation and
# UDS flash programming services 0x34 / 0x35 / 0x36 / 0x37).

add_library(uds_bootloader STATIC
    flash.c
    flash_services.c
)

target_include_directories(uds_bootloader
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src/include
)
