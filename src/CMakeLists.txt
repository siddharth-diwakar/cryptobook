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

# --- asmm_exchange: Binance wire-format parsing + reconstruction protocol.
add_library(asmm_exchange STATIC
  exchange/binance_depth.cpp
  exchange/reconstructor.cpp)
target_include_directories(asmm_exchange PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(asmm_exchange
  PUBLIC asmm_core
  PRIVATE asmm_warnings simdjson::simdjson)

# --- asmm_net: Beast/OpenSSL network transports (WSS + HTTPS). Isolated so that
# engine/exchange tests never need OpenSSL. Boost headers are marked SYSTEM so
# their warnings don't trip our -Werror.
add_library(asmm_net STATIC
  exchange/ws_client.cpp
  exchange/rest_client.cpp)
target_include_directories(asmm_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(asmm_net
  PUBLIC asmm_core
  PRIVATE asmm_warnings Boost::beast OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
get_target_property(_beast_inc Boost::beast INTERFACE_INCLUDE_DIRECTORIES)
if(_beast_inc)
  target_include_directories(asmm_net SYSTEM PRIVATE ${_beast_inc})
endif()

# --- asmm_strategy: pure Avellaneda-Stoikov strategy logic (no I/O). Depends only
# on asmm_core.
add_library(asmm_strategy STATIC
  strategy/as_quoter.cpp
  strategy/sigma_ewma.cpp
  strategy/paper_fills.cpp
  strategy/strategy.cpp)
target_include_directories(asmm_strategy PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(asmm_strategy PUBLIC asmm_core PRIVATE asmm_warnings)

# --- asmm_engine: pure engine logic (no direct network I/O; network arrives via
# transport interfaces). Testable in CI without OpenSSL/Beast.
add_library(asmm_engine STATIC
  engine/gap_verifier.cpp
  engine/backoff.cpp
  engine/crosscheck.cpp
  engine/event_log.cpp
  engine/replay.cpp
  engine/market_data_thread.cpp
  engine/engine.cpp)
target_include_directories(asmm_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(asmm_engine
  PUBLIC asmm_core asmm_exchange asmm_strategy
  PRIVATE asmm_warnings spdlog::spdlog)

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
  asmm_engine
  asmm_net
  spdlog::spdlog
  tomlplusplus::tomlplusplus)
