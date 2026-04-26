# ── src/ecusim/CMakeLists.txt ───────────────────────────────────────────────
# Builds the `ecusim` ECU simulator executable.

add_executable(ecusim
    main.c
    ecusim.c
)

target_include_directories(ecusim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src/include
)

target_link_libraries(ecusim PRIVATE
    uds_can
    uds_tp
    uds_core
    uds_bootloader
)

# Install the binary alongside the test suite
install(TARGETS ecusim RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
