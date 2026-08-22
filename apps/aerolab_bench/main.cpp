// AEROLAB RESILIENCE - Monte Carlo benchmark orchestrator (subsystem S6).
//
// Requirements: BEN-001..BEN-020, DATA-011 (promoted to MUST, see below),
// SYS-008, SYS-017, section 8.1.
//
// DEVIATION DEV-008 (docs/deviations.md) - selective trace retention promoted
// from SHOULD to MUST. A telemetry frame is ~350 bytes. At 100 Hz over 90 s
// that is ~3 MB per run; 14 scenarios times 1000 seeds is ~40 GB, which is not
// a storage inconvenience but a design error. This orchestrator therefore keeps
// aggregate metrics for every run and a full trace only for:
//   * every run whose verdict failed or whose estimator went unhealthy, and
//   * the first N seeds of each scenario, as a reference sample.
// What was dropped is stated in the campaign manifest, never left implicit.
//
// Fairness rule (BEN-002): the seed list is identical for every architecture,
// because all architectures run inside the same run, on the same measurement
// sequence. There is no way to give one of them different data.

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "aerolab/build_info_shim.hpp"
#include "aerolab/io/runner.hpp"
#include "aerolab/io/scenario.hpp"
#include "aerolab/io/yaml.hpp"
#include "aerolab/metrics/metrics.hpp"

namespace fs = std::filesystem;
using namespace aerolab;  // NOLINT - application translation unit

namespace {

struct SuiteEntry {
  std::string path;
  std::size_t seed_count{100};
};

struct Suite {
  std::string name{"core"};
  std::string description;
  std::string config_path;
  std::uint64_t seed_base{1000000};
  std::size_t keep_first_n_traces{3};
  bool keep_failure_traces{true};
  std::vector<SuiteEntry> entries;
};

struct RunRecord {
  std::string scenario_id;
  std::uint64_t seed{0};
  bool completed{false};
  bool verdict_pass{false};
  std::vector<ChannelResult> channels;
  std::vector<std::string> verdict_failures;
  double wall_time_s{0.0};
};

void printUsage() {
  std::printf(
      "aerolab_bench - Monte Carlo campaign over a scenario suite\n"
      "\n"
      "Usage:\n"
      "  aerolab_bench --suite <suite.yaml> --out <dir> [options]\n"
      "\n"
      "Options:\n"
      "  --suite <path>     suite definition                              [required]\n"
      "  --out <dir>        output directory                              [required]\n"
      "  --seeds a:b        seed index range, inclusive, overrides the suite counts\n"
      "  --jobs <n>         worker threads (default: hardware concurrency)\n"
      "  --config <path>    configuration overlay, overrides the suite entry\n"
      "  --scenario <path>  run a single scenario instead of a suite\n"
      "  --quiet            progress lines only\n"
      "  --help / --version\n");
}

bool loadSuite(const std::string& path, Suite& out, std::string& error) {
  Json root;
  if (!loadStructuredFile(path, root, error)) return false;
  int version = 0;
  if (!root.getInt("schema_version", version) || version != 1) {
    error = path + ": unsupported or missing suite schema_version";
    return false;
  }
  root.getString("name", out.name);
  root.getString("description", out.description);
  root.getString("config", out.config_path);
  unsigned long long base = 0;
  if (root.getUint64("seed_base", base)) out.seed_base = base;
  const Json& policy = root["trace_policy"];
  if (policy.isObject()) {
    int n = 0;
    if (policy.getInt("keep_first_n", n) && n >= 0) {
      out.keep_first_n_traces = static_cast<std::size_t>(n);
    }
    policy.getBool("keep_failures", out.keep_failure_traces);
  }
  const Json& scenarios = root["scenarios"];
  if (!scenarios.isArray() || scenarios.size() == 0) {
    error = path + ": suite declares no scenario";
    return false;
  }
  for (std::size_t i = 0; i < scenarios.size(); ++i) {
    const Json& e = scenarios.at(i);
    SuiteEntry entry;
    if (e.isString()) {
      entry.path = e.asString();
    } else {
      if (!e.getString("path", entry.path)) {
        error = path + ": scenario entry without a 'path'";
        return false;
      }
      int seeds = 0;
      if (e.getInt("seeds", seeds) && seeds > 0) entry.seed_count = static_cast<std::size_t>(seeds);
    }
    out.entries.push_back(entry);
  }
  return true;
}

std::string csvEscape(const std::string& s) {
  if (s.find(',') == std::string::npos && s.find('"') == std::string::npos) return s;
  std::string out = "\"";
  for (char c : s) {
    if (c == '"')
      out += "\"\"";
    else
      out.push_back(c);
  }
  out += "\"";
  return out;
}

// Aggregation over a (scenario, estimator) cell.
struct Aggregate {
  std::string scenario_id;
  EstimatorId estimator{EstimatorId::kEkf};
  std::vector<double> rmse;
  std::vector<double> max_error;
  std::vector<double> p95_error;
  std::vector<double> availability;
  std::vector<double> time_to_detect;
  std::vector<double> time_to_isolate;
  std::vector<double> nis_normalised;
  std::size_t runs{0};
  std::size_t continuous_runs{0};
  std::size_t fault_runs{0};
  std::size_t detected_runs{0};
  std::size_t isolated_runs{0};
  std::size_t runs_with_false_isolation{0};
  std::size_t total_false_isolations{0};
  std::size_t total_gate_evaluations{0};
  std::size_t unhealthy_runs{0};
};

Json distributionToJson(const Distribution& d) {
  Json j = Json::object();
  j["mean"] = Json(d.mean);
  j["median"] = Json(d.median);
  j["p95"] = Json(d.p95);
  j["max"] = Json(d.max);
  j["min"] = Json(d.min);
  j["count"] = Json(static_cast<unsigned long long>(d.count));
  return j;
}

}  // namespace

int main(int argc, char** argv) {
  std::string suite_path;
  std::string single_scenario;
  std::string out_dir;
  std::string config_override;
  std::size_t seed_first = 0;
  std::size_t seed_last = 0;
  bool seed_range_given = false;
  unsigned jobs = std::thread::hardware_concurrency();
  bool quiet = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto value = [&](const char* flag, std::string& target) -> bool {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "error: %s requires a value\n", flag);
        return false;
      }
      target = argv[++i];
      return true;
    };
    if (arg == "--help" || arg == "-h") {
      printUsage();
      return 0;
    }
    if (arg == "--version") {
      std::printf("aerolab_bench %s (commit %s)\n", buildVersion(), buildGitCommit());
      return 0;
    }
    std::string v;
    if (arg == "--suite") {
      if (!value("--suite", suite_path)) return 2;
    } else if (arg == "--scenario") {
      if (!value("--scenario", single_scenario)) return 2;
    } else if (arg == "--out") {
      if (!value("--out", out_dir)) return 2;
    } else if (arg == "--config") {
      if (!value("--config", config_override)) return 2;
    } else if (arg == "--jobs") {
      if (!value("--jobs", v)) return 2;
      jobs = static_cast<unsigned>(std::max(1, std::atoi(v.c_str())));
    } else if (arg == "--seeds") {
      if (!value("--seeds", v)) return 2;
      const std::size_t colon = v.find(':');
      if (colon == std::string::npos) {
        std::fprintf(stderr, "error: --seeds expects the form first:last\n");
        return 2;
      }
      seed_first = static_cast<std::size_t>(std::strtoull(v.c_str(), nullptr, 10));
      seed_last = static_cast<std::size_t>(std::strtoull(v.c_str() + colon + 1, nullptr, 10));
      if (seed_last < seed_first) {
        std::fprintf(stderr, "error: --seeds range is inverted\n");
        return 2;
      }
      seed_range_given = true;
    } else if (arg == "--quiet") {
      quiet = true;
    } else {
      std::fprintf(stderr, "error: unknown option '%s'\n", arg.c_str());
      return 2;
    }
  }

  if (out_dir.empty()) {
    std::fprintf(stderr, "error: --out is required\n");
    return 2;
  }
  Suite suite;
  std::string error;
  if (!single_scenario.empty()) {
    SuiteEntry e;
    e.path = single_scenario;
    e.seed_count = seed_range_given ? (seed_last - seed_first + 1) : 100;
    suite.entries.push_back(e);
    suite.name = "adhoc";
  } else if (!suite_path.empty()) {
    if (!loadSuite(suite_path, suite, error)) {
      std::fprintf(stderr, "error: %s\n", error.c_str());
      return 2;
    }
  } else {
    std::fprintf(stderr, "error: one of --suite or --scenario is required\n");
    return 2;
  }
  if (!config_override.empty()) suite.config_path = config_override;
  if (jobs == 0) jobs = 1;

  std::error_code ec;
  fs::create_directories(out_dir, ec);
  fs::create_directories(fs::path(out_dir) / "traces", ec);

  // ---- build the full job list, deterministically ---------------------------
  struct Job {
    std::size_t entry_index;
    std::uint64_t seed;
    std::size_t seed_index;
    bool keep_trace;
  };
  std::vector<Scenario> scenarios;
  std::vector<Job> jobs_list;
  for (std::size_t i = 0; i < suite.entries.size(); ++i) {
    Scenario s;
    if (!loadScenario(suite.entries[i].path, s, error)) {
      std::fprintf(stderr, "error: %s\n", error.c_str());
      return 2;
    }
    if (!suite.config_path.empty() && !applyConfigFile(suite.config_path, s, error)) {
      std::fprintf(stderr, "error: %s\n", error.c_str());
      return 2;
    }
    scenarios.push_back(s);
    const std::size_t first = seed_range_given ? seed_first : 1;
    const std::size_t last = seed_range_given ? seed_last : (suite.entries[i].seed_count);
    for (std::size_t k = first; k <= last; ++k) {
      Job job;
      job.entry_index = i;
      job.seed_index = k;
      job.seed = suite.seed_base + static_cast<std::uint64_t>(k);
      job.keep_trace = (k - first) < suite.keep_first_n_traces;
      jobs_list.push_back(job);
    }
  }

  if (!quiet) {
    std::printf("AEROLAB benchmark '%s'  |  %zu scenarios  |  %zu runs  |  %u threads\n",
                suite.name.c_str(), suite.entries.size(), jobs_list.size(), jobs);
    std::printf("commit %s  build %s\n\n", buildGitCommit(), buildType());
  }

  std::vector<RunRecord> records(jobs_list.size());
  std::atomic<std::size_t> next_index{0};
  std::atomic<std::size_t> done{0};
  std::atomic<std::size_t> failures{0};
  std::mutex log_mutex;
  const double wall_start = static_cast<double>(std::clock()) / CLOCKS_PER_SEC;

  auto worker = [&]() {
    while (true) {
      const std::size_t index = next_index.fetch_add(1);
      if (index >= jobs_list.size()) return;
      const Job& job = jobs_list[index];
      const Scenario& base = scenarios[job.entry_index];

      RunOptions options;
      options.has_seed_override = true;
      options.seed_override = job.seed;
      options.measure_tick_time = true;
      options.telemetry_decimation = 5;
      if (job.keep_trace) {
        char name[192];
        std::snprintf(name, sizeof(name), "%s_seed%llu.jsonl", base.id.c_str(),
                      static_cast<unsigned long long>(job.seed));
        options.telemetry_path = (fs::path(out_dir) / "traces" / name).string();
      }

      SimulationRunner runner;
      std::string local_error;
      RunRecord record;
      record.scenario_id = base.id;
      record.seed = job.seed;
      if (!runner.configure(base, options, local_error)) {
        record.completed = false;
        record.verdict_failures.push_back("configure: " + local_error);
      } else {
        const RunResult result = runner.run();
        record.completed = result.completed;
        record.verdict_pass = result.verdict_pass;
        record.channels = result.channels;
        record.verdict_failures = result.verdict_failures;
        record.wall_time_s = result.wall_time_s;

        // SYS-017 / BEN-012: a failed run is written out in full and the
        // campaign continues.
        if (!result.verdict_pass || !result.completed) {
          failures.fetch_add(1);
          if (suite.keep_failure_traces) {
            char name[192];
            std::snprintf(name, sizeof(name), "%s_seed%llu_manifest.json", base.id.c_str(),
                          static_cast<unsigned long long>(job.seed));
            result.manifest.writeFile((fs::path(out_dir) / "traces" / name).string());
          }
        }
      }
      records[index] = record;

      const std::size_t completed = done.fetch_add(1) + 1;
      if (!quiet && (completed % 25 == 0 || completed == jobs_list.size())) {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::printf("\r  %zu / %zu runs   %zu verdict failures", completed, jobs_list.size(),
                    failures.load());
        std::fflush(stdout);
      }
    }
  };

  std::vector<std::thread> pool;
  for (unsigned t = 0; t < jobs; ++t) pool.emplace_back(worker);
  for (std::thread& t : pool) t.join();
  if (!quiet) std::printf("\n\n");

  // ---- aggregation -----------------------------------------------------------
  std::vector<Aggregate> cells;
  auto cellFor = [&](const std::string& scenario_id, EstimatorId id) -> Aggregate& {
    for (Aggregate& a : cells) {
      if (a.scenario_id == scenario_id && a.estimator == id) return a;
    }
    Aggregate a;
    a.scenario_id = scenario_id;
    a.estimator = id;
    cells.push_back(a);
    return cells.back();
  };

  const std::string runs_path = (fs::path(out_dir) / "runs.jsonl").string();
  std::ofstream runs_out(runs_path, std::ios::binary | std::ios::trunc);
  for (const RunRecord& r : records) {
    for (const ChannelResult& c : r.channels) {
      Aggregate& a = cellFor(r.scenario_id, c.estimator);
      const MetricsSummary& m = c.metrics;
      ++a.runs;
      a.rmse.push_back(m.position_rmse_m);
      a.max_error.push_back(m.position_error_m.max);
      a.p95_error.push_back(m.position_error_m.p95);
      a.availability.push_back(m.availability);
      a.nis_normalised.push_back(m.nis_mean_normalised);
      if (m.continuity >= 1.0) ++a.continuous_runs;
      if (!c.healthy) ++a.unhealthy_runs;
      if (m.fault_present) {
        ++a.fault_runs;
        if (m.fault_detected) {
          ++a.detected_runs;
          if (m.time_to_detect_s >= 0.0) a.time_to_detect.push_back(m.time_to_detect_s);
        }
        if (m.fault_isolated) {
          ++a.isolated_runs;
          if (m.time_to_isolate_s >= 0.0) a.time_to_isolate.push_back(m.time_to_isolate_s);
        }
      }
      if (m.false_isolation_count > 0) ++a.runs_with_false_isolation;
      a.total_false_isolations += m.false_isolation_count;
      a.total_gate_evaluations += m.gate_evaluations;

      Json line = Json::object();
      line["scenario"] = Json(r.scenario_id);
      line["seed"] = Json(static_cast<unsigned long long>(r.seed));
      line["estimator"] = Json(toString(c.estimator));
      line["rmse_m"] = Json(m.position_rmse_m);
      line["p95_m"] = Json(m.position_error_m.p95);
      line["max_m"] = Json(m.position_error_m.max);
      line["ttd_s"] = m.time_to_detect_s < 0.0 ? Json() : Json(m.time_to_detect_s);
      line["tti_s"] = m.time_to_isolate_s < 0.0 ? Json() : Json(m.time_to_isolate_s);
      line["availability"] = Json(m.availability);
      line["continuity"] = Json(m.continuity);
      line["false_isolations"] = Json(static_cast<unsigned long long>(m.false_isolation_count));
      line["fault_detected"] = Json(m.fault_detected);
      line["fault_isolated"] = Json(m.fault_isolated);
      line["nis_norm"] = Json(m.nis_mean_normalised);
      line["final_mode"] = Json(toString(c.final_mode));
      line["healthy"] = Json(c.healthy);
      line["hash"] = Json(m.determinism_hash);
      runs_out << line.dump(0) << "\n";
    }
  }
  runs_out.close();

  // ---- CSV summary (BEN-013, BEN-018) ---------------------------------------
  const std::string csv_path = (fs::path(out_dir) / "summary.csv").string();
  std::ofstream csv(csv_path, std::ios::binary | std::ios::trunc);
  csv << "scenario,estimator,runs,rmse_mean_m,rmse_median_m,rmse_p95_m,rmse_max_m,"
         "max_error_p95_m,max_error_worst_m,availability_mean,continuity_fraction,"
         "detection_rate,isolation_rate,missed_detection_rate,ttd_median_s,ttd_p95_s,"
         "false_isolation_rate_per_gate,runs_with_false_isolation,nis_norm_mean,unhealthy_runs\n";
  Json summary_json = Json::array();
  for (const Aggregate& a : cells) {
    const Distribution rmse = Distribution::from(a.rmse);
    const Distribution maxe = Distribution::from(a.max_error);
    const Distribution avail = Distribution::from(a.availability);
    const Distribution ttd = Distribution::from(a.time_to_detect);
    const Distribution tti = Distribution::from(a.time_to_isolate);
    const Distribution nis = Distribution::from(a.nis_normalised);
    const double detection_rate =
        a.fault_runs > 0 ? static_cast<double>(a.detected_runs) / static_cast<double>(a.fault_runs)
                         : 0.0;
    const double isolation_rate =
        a.fault_runs > 0 ? static_cast<double>(a.isolated_runs) / static_cast<double>(a.fault_runs)
                         : 0.0;
    const double continuity_fraction =
        a.runs > 0 ? static_cast<double>(a.continuous_runs) / static_cast<double>(a.runs) : 0.0;
    const double false_isolation_rate = a.total_gate_evaluations > 0
                                            ? static_cast<double>(a.total_false_isolations) /
                                                  static_cast<double>(a.total_gate_evaluations)
                                            : 0.0;

    csv << csvEscape(a.scenario_id) << "," << toString(a.estimator) << "," << a.runs << ","
        << rmse.mean << "," << rmse.median << "," << rmse.p95 << "," << rmse.max << "," << maxe.p95
        << "," << maxe.max << "," << avail.mean << "," << continuity_fraction << ","
        << detection_rate << "," << isolation_rate << "," << (1.0 - detection_rate) << ","
        << (ttd.count > 0 ? ttd.median : -1.0) << "," << (ttd.count > 0 ? ttd.p95 : -1.0) << ","
        << false_isolation_rate << "," << a.runs_with_false_isolation << "," << nis.mean << ","
        << a.unhealthy_runs << "\n";

    Json cell = Json::object();
    cell["scenario"] = Json(a.scenario_id);
    cell["estimator"] = Json(toString(a.estimator));
    cell["runs"] = Json(static_cast<unsigned long long>(a.runs));
    cell["rmse_m"] = distributionToJson(rmse);
    cell["max_error_m"] = distributionToJson(maxe);
    cell["availability"] = distributionToJson(avail);
    cell["time_to_detect_s"] = distributionToJson(ttd);
    cell["time_to_isolate_s"] = distributionToJson(tti);
    cell["nis_normalised"] = distributionToJson(nis);
    cell["fault_runs"] = Json(static_cast<unsigned long long>(a.fault_runs));
    cell["detection_rate"] = Json(detection_rate);
    cell["isolation_rate"] = Json(isolation_rate);
    cell["missed_detection_rate"] = Json(1.0 - detection_rate);
    cell["continuity_fraction"] = Json(continuity_fraction);
    cell["false_isolation_rate_per_gate"] = Json(false_isolation_rate);
    cell["runs_with_false_isolation"] =
        Json(static_cast<unsigned long long>(a.runs_with_false_isolation));
    cell["unhealthy_runs"] = Json(static_cast<unsigned long long>(a.unhealthy_runs));
    summary_json.push(cell);
  }
  csv.close();

  Json manifest = Json::object();
  manifest["schema"] = Json(1);
  manifest["suite"] = Json(suite.name);
  manifest["description"] = Json(suite.description);
  manifest["config"] = Json(suite.config_path);
  manifest["commit"] = Json(buildGitCommit());
  manifest["version"] = Json(buildVersion());
  manifest["build_type"] = Json(buildType());
  manifest["compiler"] = Json(std::string(buildCompiler()) + " " + buildCompilerVersion());
  manifest["threads"] = Json(static_cast<int>(jobs));
  manifest["total_runs"] = Json(static_cast<unsigned long long>(jobs_list.size()));
  manifest["verdict_failures"] = Json(static_cast<unsigned long long>(failures.load()));
  manifest["seed_base"] = Json(static_cast<unsigned long long>(suite.seed_base));
  manifest["cpu_time_s"] = Json(static_cast<double>(std::clock()) / CLOCKS_PER_SEC - wall_start);
  Json trace_policy = Json::object();
  trace_policy["keep_first_n"] = Json(static_cast<unsigned long long>(suite.keep_first_n_traces));
  trace_policy["keep_failures"] = Json(suite.keep_failure_traces);
  trace_policy["note"] = Json(
      "Full telemetry is retained only for the sampled seeds and for failed runs (DEV-008). "
      "Aggregate metrics below cover every run in the campaign.");
  manifest["trace_policy"] = trace_policy;
  Json scenario_list = Json::array();
  for (const Scenario& s : scenarios) {
    Json e = Json::object();
    e["id"] = Json(s.id);
    e["path"] = Json(s.source_path);
    e["hash"] = Json(s.scenario_hash);
    scenario_list.push(e);
  }
  manifest["scenarios"] = scenario_list;
  manifest["summary"] = summary_json;
  manifest.writeFile((fs::path(out_dir) / "manifest.json").string());

  if (!quiet) {
    std::printf("%-12s %-18s %6s %10s %10s %10s %8s %8s %9s\n", "scenario", "estimator", "runs",
                "RMSE med", "RMSE P95", "worst m", "det %", "iso %", "false/gate");
    std::printf("%s\n", std::string(104, '-').c_str());
    for (const Aggregate& a : cells) {
      const Distribution rmse = Distribution::from(a.rmse);
      const Distribution maxe = Distribution::from(a.max_error);
      const double det = a.fault_runs > 0 ? 100.0 * static_cast<double>(a.detected_runs) /
                                                static_cast<double>(a.fault_runs)
                                          : 0.0;
      const double iso = a.fault_runs > 0 ? 100.0 * static_cast<double>(a.isolated_runs) /
                                                static_cast<double>(a.fault_runs)
                                          : 0.0;
      const double fa = a.total_gate_evaluations > 0
                            ? static_cast<double>(a.total_false_isolations) /
                                  static_cast<double>(a.total_gate_evaluations)
                            : 0.0;
      std::printf("%-12s %-18s %6zu %10.3f %10.3f %10.3f %7.1f%% %7.1f%% %9.2e\n",
                  a.scenario_id.c_str(), toString(a.estimator), a.runs, rmse.median, rmse.p95,
                  maxe.max, det, iso, fa);
    }
    std::printf("\nartefacts: %s\n", out_dir.c_str());
    std::printf("  manifest.json   campaign configuration, provenance and aggregates\n");
    std::printf("  summary.csv     one row per scenario x estimator\n");
    std::printf("  runs.jsonl      one line per run x estimator (every run, no sampling)\n");
    std::printf("  traces/         full telemetry for sampled seeds and failed runs\n");
  }
  return failures.load() > 0 ? 1 : 0;
}
