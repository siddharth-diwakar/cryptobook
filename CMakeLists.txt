cmake_minimum_required(VERSION 3.22)

project(asmm
        VERSION 0.1.0
        DESCRIPTION "Avellaneda-Stoikov market maker for BTC/USDT on Binance testnet"
        LANGUAGES CXX)

# Extracted-archive timestamp policy (silences CMP0135 on CMake >= 3.24).
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# --- Language standard ---
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Default to Release so a bare `cmake -B build` matches the docs ---
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No CMAKE_BUILD_TYPE set; defaulting to Release")
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# --- The binary must land at build/asmm (README documents ./build/asmm) ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# --- Tooling: emit compile_commands.json for clangd/clang-tidy ---
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Capture git short SHA for the version string (configure-time snapshot) ---
find_package(Git QUIET)
set(ASMM_GIT_SHA "nogit")
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE ASMM_GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(ASMM_GIT_SHA STREQUAL "")
    set(ASMM_GIT_SHA "nogit")
  endif()
endif()

include(cmake/Warnings.cmake)
include(cmake/Dependencies.cmake)

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)

option(ASMM_BUILD_BENCH "Build benchmarks" OFF)
if(ASMM_BUILD_BENCH)
  add_subdirectory(bench)
endif()
