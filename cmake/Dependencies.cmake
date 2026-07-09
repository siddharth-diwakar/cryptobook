include(FetchContent)

# All third-party deps are pinned by release tag/URL and fetched at configure
# time. Keep this list minimal — one dependency per job (CLAUDE.md).

# --- spdlog: logging ---
FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.14.1
  GIT_SHALLOW TRUE)

# --- Catch2: unit + replay tests ---
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.7.1
  GIT_SHALLOW TRUE)

# --- simdjson: fast JSON parsing for WS payloads ---
FetchContent_Declare(simdjson
  GIT_REPOSITORY https://github.com/simdjson/simdjson.git
  GIT_TAG v3.10.1
  GIT_SHALLOW TRUE)

# --- toml++: config parsing (startup only, never the hot path) ---
FetchContent_Declare(tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG v3.4.0
  GIT_SHALLOW TRUE)

# --- Boost (Beast/Asio/System): WebSocket + HTTP client, used from Phase 2 ---
# Scoped to just the libraries we need so we don't build all of Boost.
set(BOOST_INCLUDE_LIBRARIES beast asio system)
set(BOOST_ENABLE_CMAKE ON)
FetchContent_Declare(Boost
  URL https://github.com/boostorg/boost/releases/download/boost-1.86.0/boost-1.86.0-cmake.tar.xz)

FetchContent_MakeAvailable(spdlog Catch2 simdjson tomlplusplus Boost)

# Expose Catch2's CMake helpers (catch_discover_tests).
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
