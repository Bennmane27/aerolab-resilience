// AEROLAB RESILIENCE - single scenario runner.
//
// Requirements: API-005 (non-zero exit on invalid configuration), API-006
// (--help and --version), SYS-016 (exports a conformance verdict).
//
// Exit codes
//   0  the run completed and every acceptance criterion passed
//   1  the run completed but at least one acceptance criterion failed
//   2  the configuration was rejected, or the run could not complete

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "aerolab/build_info_shim.hpp"
#include "aerolab/io/runner.hpp"
#include "aerolab/io/scenario.hpp"

namespace {

void printUsage() {
  std::printf(
      "aerolab_cli - run one AEROLAB RESILIENCE scenario\n"
      "\n"
      "Usage:\n"
      "  aerolab_cli --scenario <file.yaml> [options]\n"
      "\n"
      "Options:\n"
      "  --scenario <path>     scenario file (.yaml or .json)            [required]\n"
      "  --config <path>       estimator / integrity configuration overlay\n"
      "  --seed <n>            override the scenario seed\n"
      "  --out <dir>           directory for the manifest and telemetry\n"
      "  --telemetry           record a JSONL trace of the run\n"
      "  --decimate <n>        record one frame in n (default 5)\n"
      "  --no-timing           skip per-tick wall clock measurement\n"
      "  --quiet               only print the verdict line\n"
      "  --help                this text\n"
      "  --version             version, commit and build type\n"
      "\n"
      "Exit codes: 0 pass, 1 acceptance failure, 2 configuration or run error.\n");
}

bool nextValue(int argc, char** argv, int& i, const char* flag, std::string& out) {
  if (i + 1 >= argc) {
    std::fprintf(stderr, "error: %s requires a value\n", flag);
    return false;
  }
  out = argv[++i];
  return true;
}

std::string joinPath(const std::string& dir, const std::string& file) {
  if (dir.empty()) return file;
  const char last = dir[dir.size() - 1];
  return (last == '/' || last == '\\') ? dir + file : dir + "/" + file;
}

}  // namespace

int main(int argc, char** argv) {
  std::string scenario_path;
  std::string config_path;
  std::string out_dir = "results";
  bool telemetry = false;
  bool quiet = false;
  aerolab::RunOptions options;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    std::string value;
    if (arg == "--help" || arg == "-h") {
      printUsage();
      return 0;
    }
    if (arg == "--version") {
      std::printf("aerolab %s (commit %s, %s build, %s %s)\n", aerolab::buildVersion(),
                  aerolab::buildGitCommit(), aerolab::buildType(), aerolab::buildCompiler(),
                  aerolab::buildCompilerVersion());
      return 0;
    }
    if (arg == "--scenario") {
      if (!nextValue(argc, argv, i, "--scenario", scenario_path)) return 2;
    } else if (arg == "--config") {
      if (!nextValue(argc, argv, i, "--config", config_path)) return 2;
    } else if (arg == "--seed") {
      if (!nextValue(argc, argv, i, "--seed", value)) return 2;
      options.has_seed_override = true;
      options.seed_override = std::strtoull(value.c_str(), nullptr, 10);
    } else if (arg == "--out") {
      if (!nextValue(argc, argv, i, "--out", out_dir)) return 2;
    } else if (arg == "--decimate") {
      if (!nextValue(argc, argv, i, "--decimate", value)) return 2;
      options.telemetry_decimation = std::atoi(value.c_str());
      if (options.telemetry_decimation < 1) options.telemetry_decimation = 1;
    } else if (arg == "--telemetry") {
      telemetry = true;
    } else if (arg == "--no-timing") {
      options.measure_tick_time = false;
    } else if (arg == "--quiet") {
      quiet = true;
    } else {
      std::fprintf(stderr, "error: unknown option '%s' (try --help)\n", arg.c_str());
      return 2;
    }
  }

  if (scenario_path.empty()) {
    std::fprintf(stderr, "error: --scenario is required (try --help)\n");
    return 2;
  }

  aerolab::Scenario scenario;
  std::string error;
  if (!aerolab::loadScenario(scenario_path, scenario, error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 2;
  }
  if (!config_path.empty() && !aerolab::applyConfigFile(config_path, scenario, error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 2;
  }

  const std::uint64_t seed = options.has_seed_override ? options.seed_override : scenario.seed;
  char stem[128];
  std::snprintf(stem, sizeof(stem), "%s_seed%llu", scenario.id.c_str(),
                static_cast<unsigned long long>(seed));
  if (telemetry) options.telemetry_path = joinPath(out_dir, std::string(stem) + "_telemetry.jsonl");

  aerolab::SimulationRunner runner;
  if (!runner.configure(scenario, options, error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 2;
  }

  const aerolab::RunResult result = runner.run();
  if (!result.error.empty()) {
    std::fprintf(stderr, "error: %s\n", result.error.c_str());
    return 2;
  }

  const std::string manifest_path = joinPath(out_dir, std::string(stem) + "_manifest.json");
  if (!result.manifest.writeFile(manifest_path)) {
    std::fprintf(stderr, "error: cannot write %s (does the directory exist?)\n",
                 manifest_path.c_str());
    return 2;
  }

  if (!quiet) {
    std::printf("\n%s  %s\n", scenario.id.c_str(), scenario.name.c_str());
    std::printf("seed %llu   duration %.1f s   commit %s\n",
                static_cast<unsigned long long>(result.seed), scenario.duration_s,
                aerolab::buildGitCommit());
    if (result.fault_start_s >= 0.0) {
      std::printf("fault window %.2f s .. %.2f s\n", result.fault_start_s, result.fault_end_s);
    } else {
      std::printf("no fault injected (nominal run)\n");
    }
    std::printf("\n%-18s %10s %10s %10s %8s %8s %7s  %-14s\n", "estimator", "RMSE m", "P95 m",
                "max m", "TTD s", "TTI s", "avail", "final mode");
    std::printf("%s\n", std::string(96, '-').c_str());
    for (const aerolab::ChannelResult& c : result.channels) {
      const aerolab::MetricsSummary& m = c.metrics;
      char ttd[16];
      char tti[16];
      if (m.time_to_detect_s < 0.0) {
        std::snprintf(ttd, sizeof(ttd), "%8s", "-");
      } else {
        std::snprintf(ttd, sizeof(ttd), "%8.2f", m.time_to_detect_s);
      }
      if (m.time_to_isolate_s < 0.0) {
        std::snprintf(tti, sizeof(tti), "%8s", "-");
      } else {
        std::snprintf(tti, sizeof(tti), "%8.2f", m.time_to_isolate_s);
      }
      std::printf("%-18s %10.3f %10.3f %10.3f %s %s %6.1f%%  %-14s\n",
                  aerolab::toString(c.estimator), m.position_rmse_m, m.position_error_m.p95,
                  m.position_error_m.max, ttd, tti, m.availability * 100.0,
                  aerolab::toString(c.final_mode));
    }
    std::printf("\ntick p95 %.4f ms   peak RSS %.1f MB   wall %.2f s\n",
                result.channels.empty() ? 0.0 : result.channels[0].metrics.tick_time_ms.p95,
                result.channels.empty() ? 0.0 : result.channels[0].metrics.peak_memory_mb,
                result.wall_time_s);
    std::printf("manifest: %s\n", manifest_path.c_str());
    if (!options.telemetry_path.empty()) {
      std::printf("telemetry: %s\n", options.telemetry_path.c_str());
    }
  }

  if (!result.verdict_pass) {
    std::printf("\nVERDICT: FAIL\n");
    for (const std::string& f : result.verdict_failures) {
      std::printf("  - %s\n", f.c_str());
    }
    return 1;
  }
  std::printf("\nVERDICT: PASS\n");
  return 0;
}
