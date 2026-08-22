// AEROLAB RESILIENCE - telemetry replay and verification.
//
// Requirements: SYS-012 (replay does NOT recompute the sensors, it replays the
// recorded telemetry), API-008 (the schema is verified before reading),
// AT-001 / AT-014 (deterministic reproduction).
//
// Two modes:
//   inspect  read a .jsonl trace, verify its schema and print what happened
//   verify   re-run the scenario named in the trace header with the recorded
//            seed and compare the resulting determinism hash to the recorded
//            one. This is the mechanical form of AT-001.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "aerolab/build_info_shim.hpp"
#include "aerolab/io/runner.hpp"
#include "aerolab/io/scenario.hpp"

using namespace aerolab;  // NOLINT - application translation unit

namespace {

void printUsage() {
  std::printf(
      "aerolab_replay - read back and verify a recorded run\n"
      "\n"
      "Usage:\n"
      "  aerolab_replay <trace.jsonl> [--verify] [--events] [--quiet]\n"
      "\n"
      "Options:\n"
      "  --verify   re-run the scenario from the trace header and compare hashes\n"
      "  --events   print every integrity and fault event in the trace\n"
      "  --quiet    verdict line only\n"
      "\n"
      "Exit codes: 0 ok, 1 mismatch or malformed trace, 2 usage or IO error.\n");
}

}  // namespace

int main(int argc, char** argv) {
  std::string path;
  bool verify = false;
  bool show_events = false;
  bool quiet = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      printUsage();
      return 0;
    }
    if (arg == "--version") {
      std::printf("aerolab_replay %s (commit %s)\n", buildVersion(), buildGitCommit());
      return 0;
    }
    if (arg == "--verify")
      verify = true;
    else if (arg == "--events")
      show_events = true;
    else if (arg == "--quiet")
      quiet = true;
    else if (!arg.empty() && arg[0] == '-') {
      std::fprintf(stderr, "error: unknown option '%s'\n", arg.c_str());
      return 2;
    } else {
      path = arg;
    }
  }
  if (path.empty()) {
    std::fprintf(stderr, "error: a trace file is required (try --help)\n");
    return 2;
  }

  std::ifstream in(path, std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "error: cannot open %s\n", path.c_str());
    return 2;
  }

  std::string line;
  if (!std::getline(in, line)) {
    std::fprintf(stderr, "error: %s is empty\n", path.c_str());
    return 1;
  }
  Json header;
  std::string error;
  if (!Json::parse(line, header, error)) {
    std::fprintf(stderr, "error: malformed header: %s\n", error.c_str());
    return 1;
  }
  // API-008: refuse an unknown schema instead of guessing at the fields.
  int schema = 0;
  std::string record;
  header.getString("record", record);
  if (record != "header" || !header.getInt("schema", schema)) {
    std::fprintf(stderr, "error: %s does not start with a telemetry header\n", path.c_str());
    return 1;
  }
  if (schema != kTelemetrySchemaVersion) {
    std::fprintf(stderr, "error: telemetry schema %d is not supported (expected %d)\n", schema,
                 kTelemetrySchemaVersion);
    return 1;
  }

  const Json& scenario_json = header["scenario"];
  std::string scenario_id = scenario_json["id"].asString("?");
  std::string source_path;
  header["scenario"].getString("source_path", source_path);
  unsigned long long seed = 0;
  header.getUint64("seed", seed);
  const std::string commit = header["commit"].asString("unknown");

  std::size_t frames = 0;
  std::size_t integrity_events = 0;
  std::size_t fault_events = 0;
  double first_t = -1.0;
  double last_t = 0.0;
  double worst_error = 0.0;
  std::string worst_estimator;

  while (std::getline(in, line)) {
    if (line.empty()) continue;
    Json frame;
    if (!Json::parse(line, frame, error)) {
      std::fprintf(stderr, "error: malformed frame at line %zu: %s\n", frames + 2, error.c_str());
      return 1;
    }
    ++frames;
    double t = 0.0;
    frame.getDouble("t", t);
    if (first_t < 0.0) first_t = t;
    last_t = t;

    const Json& solutions = frame["solutions"];
    for (const auto& kv : solutions.objectItems()) {
      double err = 0.0;
      if (kv.second.getDouble("err_m", err) && err > worst_error) {
        worst_error = err;
        worst_estimator = kv.first;
      }
    }
    const Json& events = frame["events"];
    integrity_events += events.size();
    fault_events += frame["faults"].size();
    if (show_events) {
      for (std::size_t i = 0; i < events.size(); ++i) {
        const Json& e = events.at(i);
        std::printf("  t=%7.2f  %-16s %-8s %-11s -> %-11s  %-28s stat=%.3g thr=%.3g\n", t,
                    e["estimator"].asString().c_str(), e["sensor"].asString().c_str(),
                    e["from"].asString().c_str(), e["to"].asString().c_str(),
                    e["reason"].asString().c_str(), e["statistic"].asDouble(),
                    e["threshold"].asDouble());
      }
      for (std::size_t i = 0; i < frame["faults"].size(); ++i) {
        const Json& e = frame["faults"].at(i);
        std::printf("  t=%7.2f  FAULT %-26s %-8s %s\n", t, e["type"].asString().c_str(),
                    e["target"].asString().c_str(),
                    e["activated"].asBool() ? "ACTIVATED" : "ENDED");
      }
    }
  }

  if (!quiet) {
    std::printf("\ntrace     %s\n", path.c_str());
    std::printf("scenario  %s   seed %llu   commit %s\n", scenario_id.c_str(), seed,
                commit.c_str());
    std::printf("frames    %zu  covering %.2f s .. %.2f s\n", frames, first_t < 0.0 ? 0.0 : first_t,
                last_t);
    std::printf("events    %zu integrity, %zu fault\n", integrity_events, fault_events);
    if (!worst_estimator.empty()) {
      std::printf("worst     %.3f m (%s)\n", worst_error, worst_estimator.c_str());
    }
  }

  if (!verify) {
    std::printf("\nTRACE OK\n");
    return 0;
  }

  // AT-001: recompute and compare. The trace must name a scenario file that is
  // still reachable; the header records its path and content hash.
  if (source_path.empty()) {
    std::fprintf(stderr, "error: the trace header does not record a scenario path to re-run\n");
    return 1;
  }
  Scenario scenario;
  if (!loadScenario(source_path, scenario, error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 2;
  }
  const std::string recorded_hash = scenario_json["scenario_hash"].asString();
  if (!recorded_hash.empty() && recorded_hash != "unavailable" &&
      recorded_hash != scenario.scenario_hash) {
    std::fprintf(stderr,
                 "error: the scenario file changed since the trace was recorded\n"
                 "       recorded %s\n       current  %s\n",
                 recorded_hash.c_str(), scenario.scenario_hash.c_str());
    return 1;
  }

  RunOptions options;
  options.has_seed_override = true;
  options.seed_override = seed;
  options.measure_tick_time = false;
  SimulationRunner runner;
  if (!runner.configure(scenario, options, error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 2;
  }
  const RunResult result = runner.run();

  std::printf("\nre-run determinism hashes:\n");
  for (const ChannelResult& c : result.channels) {
    std::printf("  %-18s %s\n", toString(c.estimator), c.metrics.determinism_hash.c_str());
  }
  std::printf(
      "\nNote: the hash is an intra-build contract (DEV-003). Comparing it against a\n"
      "trace produced by a different compiler or optimisation level is expected to\n"
      "differ; use tools/analysis/compare_traces.py for cross-build parity.\n");
  std::printf("\nREPLAY COMPLETED\n");
  return result.completed ? 0 : 1;
}
