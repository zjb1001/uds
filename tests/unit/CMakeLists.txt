# ── tests/unit/CMakeLists.txt ───────────────────────────────────────────────
# Builds and registers the uds_can unit-test binary.

find_package(PkgConfig REQUIRED)
pkg_check_modules(CHECK REQUIRED check)

add_executable(test_can_socket
    test_can_socket.c
)

target_include_directories(test_can_socket PRIVATE
    ${CHECK_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src/include
)

target_link_libraries(test_can_socket PRIVATE
    uds_can
    ${CHECK_LIBRARIES}
)

target_compile_options(test_can_socket PRIVATE
    ${CHECK_CFLAGS_OTHER}
)

# Register with CTest
add_test(
    NAME    uds_can_unit_tests
    COMMAND test_can_socket
)
