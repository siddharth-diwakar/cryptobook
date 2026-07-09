# Phase 1 benchmarks. Built only when ASMM_BUILD_BENCH=ON.
set(ASMM_DATA_DIR_BENCH "${CMAKE_SOURCE_DIR}/tests/data")
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/data/depth_btcusdt.tar.gz")
  file(ARCHIVE_EXTRACT
    INPUT "${CMAKE_SOURCE_DIR}/tests/data/depth_btcusdt.tar.gz"
    DESTINATION "${CMAKE_BINARY_DIR}/testdata")
  set(ASMM_DATA_DIR_BENCH "${CMAKE_BINARY_DIR}/testdata")
endif()

add_executable(bench_book bench_book.cpp)
target_link_libraries(bench_book PRIVATE asmm_warnings asmm_exchange simdjson::simdjson)
target_compile_definitions(bench_book PRIVATE ASMM_DATA_DIR="${ASMM_DATA_DIR_BENCH}")

add_executable(bench_spsc bench_spsc.cpp)
target_link_libraries(bench_spsc PRIVATE asmm_warnings asmm_core)
