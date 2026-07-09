# Generate version.hpp from the template into the build tree.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/core/version.hpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/generated/version.hpp
  @ONLY)

# --- asmm_core: book, SPSC queue, event types, clock. NO I/O, NO third-party.
# Dependency-free by design — this is what structurally enforces the no-I/O rule.
add_library(asmm_core STATIC
  core/book.cpp)
target_include_directories(asmm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(asmm_core PRIVATE asmm_warnings)

# --- asmm_exchange: Binance wire-format parsing (exchange boundary).
add_library(asmm_exchange STATIC
  exchange/binance_depth.cpp)
target_include_directories(asmm_exchange PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(asmm_exchange
  PUBLIC asmm_core
  PRIVATE asmm_warnings simdjson::simdjson)

# --- asmm: the application binary.
add_executable(asmm
  engine/main.cpp
  engine/config.cpp)
target_include_directories(asmm PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}                # so "engine/config.hpp" resolves
  ${CMAKE_CURRENT_BINARY_DIR}/generated)     # so "version.hpp" resolves
target_link_libraries(asmm PRIVATE
  asmm_warnings
  asmm_core
  spdlog::spdlog
  tomlplusplus::tomlplusplus)
