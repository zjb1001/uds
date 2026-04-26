cmake_minimum_required(VERSION 3.16)

project(uds-diagnostic-system
    VERSION 0.1.0
    DESCRIPTION "UDS Diagnostic Simulation System (ISO 14229 / ISO 15765-2)"
    LANGUAGES C)

# ── Standards ──────────────────────────────────────────────────────────────
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ── Compiler warnings ──────────────────────────────────────────────────────
add_compile_options(
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wformat=2
    -fstack-protector-strong
)

# ── Build options ──────────────────────────────────────────────────────────
option(ENABLE_COVERAGE "Enable gcov code-coverage instrumentation" OFF)
option(ENABLE_CAN_FD   "Enable CAN FD (64-byte frames) support"    OFF)
option(ENABLE_ASAN     "Enable AddressSanitizer"                    OFF)

if(ENABLE_COVERAGE)
    add_compile_options(-O0 -g --coverage -fprofile-arcs -ftest-coverage)
    add_link_options(--coverage)
endif()

if(ENABLE_CAN_FD)
    add_compile_definitions(CAN_FD_ENABLED)
endif()

if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# ── Install directories ────────────────────────────────────────────────────
include(GNUInstallDirs)

# ── Testing ────────────────────────────────────────────────────────────────
enable_testing()

# ── Subdirectories (added only when they exist) ────────────────────────────
if(EXISTS "${CMAKE_SOURCE_DIR}/src/CMakeLists.txt")
    add_subdirectory(src)
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()
