# ── tests/unit/CMakeLists.txt ───────────────────────────────────────────────
# Builds and registers all C unit-test binaries.

find_package(PkgConfig REQUIRED)
pkg_check_modules(CHECK REQUIRED check)

# ── uds_can unit tests ───────────────────────────────────────────────────────
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

add_test(
    NAME    uds_can_unit_tests
    COMMAND test_can_socket
)

# ── uds_tp unit tests ────────────────────────────────────────────────────────
add_executable(test_iso_tp
    test_iso_tp.c
)

target_include_directories(test_iso_tp PRIVATE
    ${CHECK_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src/include
)

target_link_libraries(test_iso_tp PRIVATE
    uds_tp
    ${CHECK_LIBRARIES}
)

target_compile_options(test_iso_tp PRIVATE
    ${CHECK_CFLAGS_OTHER}
)

add_test(
    NAME    uds_tp_unit_tests
    COMMAND test_iso_tp
)

# ── uds_core session unit tests ──────────────────────────────────────────────
add_executable(test_session
    test_session.c
)

target_include_directories(test_session PRIVATE
    ${CHECK_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src/include
)

target_link_libraries(test_session PRIVATE
    uds_core
    ${CHECK_LIBRARIES}
)

target_compile_options(test_session PRIVATE
    ${CHECK_CFLAGS_OTHER}
)

add_test(
    NAME    uds_core_session_unit_tests
    COMMAND test_session
)

# ── uds_core security unit tests ─────────────────────────────────────────────
add_executable(test_security
    test_security.c
)

target_include_directories(test_security PRIVATE
    ${CHECK_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/src/include
)

target_link_libraries(test_security PRIVATE
    uds_core
    ${CHECK_LIBRARIES}
)

target_compile_options(test_security PRIVATE
    ${CHECK_CFLAGS_OTHER}
)

add_test(
    NAME    uds_core_security_unit_tests
    COMMAND test_security
)
