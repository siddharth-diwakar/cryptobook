add_executable(asmm_tests
  test_sanity.cpp
  test_spsc_queue.cpp
  test_book.cpp
  test_binance_depth.cpp
  test_book_replay.cpp
  test_reconstructor.cpp
  test_gap_verifier.cpp
  test_backoff.cpp
  test_crosscheck.cpp
  test_market_data_thread.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/config.cpp)

target_include_directories(asmm_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src)

# Committed fixtures ship as a tar.gz to keep git history small; extract to the
# build tree at configure time (CMake >= 3.18). Falls back to raw files in
# tests/data/ (e.g. right after recording, before compression).
set(ASMM_DATA_DIR "${CMAKE_SOURCE_DIR}/tests/data")
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/data/depth_btcusdt.tar.gz")
  file(ARCHIVE_EXTRACT
    INPUT "${CMAKE_SOURCE_DIR}/tests/data/depth_btcusdt.tar.gz"
    DESTINATION "${CMAKE_BINARY_DIR}/testdata")
  set(ASMM_DATA_DIR "${CMAKE_BINARY_DIR}/testdata")
endif()

target_compile_definitions(asmm_tests PRIVATE
  ASMM_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  ASMM_DATA_DIR="${ASMM_DATA_DIR}")

target_link_libraries(asmm_tests PRIVATE
  asmm_warnings
  asmm_engine             # brings asmm_exchange + asmm_core transitively (PUBLIC)
  Catch2::Catch2WithMain
  simdjson::simdjson
  spdlog::spdlog
  tomlplusplus::tomlplusplus
  Boost::beast)

include(Catch)
catch_discover_tests(asmm_tests)
