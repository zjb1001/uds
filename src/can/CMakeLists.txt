# ── src/can/CMakeLists.txt ──────────────────────────────────────────────────
# Builds the uds_can static library (SocketCAN adapter layer).

add_library(uds_can STATIC
    can_socket.c
)

target_include_directories(uds_can
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src/include
)

# The library uses Linux SocketCAN headers; no extra link libraries needed.
# Consumers link with:   target_link_libraries(<target> PRIVATE uds_can)
