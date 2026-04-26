# ── src/tp/CMakeLists.txt ───────────────────────────────────────────────────
# Builds the uds_tp static library (ISO-TP transport layer).

add_library(uds_tp STATIC
    iso_tp.c
)

target_include_directories(uds_tp
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src/include
)

# uds_tp uses uds_can for CAN socket I/O
target_link_libraries(uds_tp
    PUBLIC uds_can
)
