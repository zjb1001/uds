# ── src/core/CMakeLists.txt ──────────────────────────────────────────────────
# Builds the uds_core static library (UDS core diagnostic services layer).
#
# Provides:
#   - Service 0x10: Diagnostic Session Control
#   - Service 0x3E: Tester Present
#   - Service 0x27: Security Access

add_library(uds_core STATIC
    nrc.c
    session.c
    security.c
    data.c
    dtc.c
    routine_control.c
)

target_include_directories(uds_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src/include
)

# uds_core depends on uds_nrc (header-only); no extra link deps needed.
# Consumers link with:   target_link_libraries(<target> PRIVATE uds_core)
