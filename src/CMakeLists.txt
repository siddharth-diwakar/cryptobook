# Generate version.hpp from the template into the build tree.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/core/version.hpp.in
  ${CMAKE_CURRENT_BINARY_DIR}/generated/version.hpp
  @ONLY)

add_executable(asmm
  engine/main.cpp
  engine/config.cpp)

target_include_directories(asmm PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}                # so "engine/config.hpp" resolves
  ${CMAKE_CURRENT_BINARY_DIR}/generated)     # so "version.hpp" resolves

target_link_libraries(asmm PRIVATE
  asmm_warnings
  spdlog::spdlog
  tomlplusplus::tomlplusplus
  simdjson::simdjson)
