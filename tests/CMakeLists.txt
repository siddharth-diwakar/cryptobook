add_executable(asmm_tests
  test_sanity.cpp
  ${CMAKE_SOURCE_DIR}/src/engine/config.cpp)

target_include_directories(asmm_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/src)

# Absolute path to the repo so tests can locate config/testnet.toml.
target_compile_definitions(asmm_tests PRIVATE
  ASMM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

target_link_libraries(asmm_tests PRIVATE
  asmm_warnings
  Catch2::Catch2WithMain
  spdlog::spdlog
  tomlplusplus::tomlplusplus
  simdjson::simdjson
  Boost::beast)

include(Catch)
catch_discover_tests(asmm_tests)
