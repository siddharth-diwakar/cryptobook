#include <spdlog/spdlog.h>

#include <cstdio>
#include <exception>
#include <string>

#include "engine/config.hpp"
#include "version.hpp"

namespace {

void PrintUsage(const char* prog) {
  spdlog::error("usage: {} [--version | --config <path>]", prog);
}

}  // namespace

int main(int argc, char** argv) {
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

  if (argc < 2) {
    PrintUsage(argv[0]);
    return 1;
  }

  const std::string arg = argv[1];

  if (arg == "--version") {
    // Plain line to stdout (no log decoration) — matches README output.
    std::printf("asmm %s (git %s, %s)\n", asmm::kVersion, asmm::kGitSha, asmm::kBuildType);
    return 0;
  }

  if (arg == "--config") {
    if (argc < 3) {
      spdlog::error("--config requires a path");
      return 1;
    }
    const std::string config_path = argv[2];
    try {
      const asmm::AppConfig cfg = asmm::LoadConfig(config_path, ".env");
      spdlog::info("loaded config: symbol={} rest_url={} ws_market_url={} log_dir={}", cfg.symbol,
                   cfg.rest_url, cfg.ws_market_url, cfg.log_dir);
      spdlog::info("api key loaded: {} (len {})", cfg.api_key.empty() ? "no" : "yes",
                   cfg.api_key.size());
      return 0;
    } catch (const std::exception& e) {
      spdlog::error("config error: {}", e.what());
      return 1;
    }
  }

  PrintUsage(argv[0]);
  return 1;
}
