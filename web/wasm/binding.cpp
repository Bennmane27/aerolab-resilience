// AEROLAB RESILIENCE - WebAssembly binding.
//
// Requirements: SYS-006 (same C++ core native and in the browser), API-001
// (create / reset / step / loadScenario / getFrame), API-002 (no direct
// mutation of the ground truth from the UI), API-003 (C++ errors become
// structured TypeScript errors), API-004 (versioned telemetry schema),
// SYS-009 (works offline once loaded).
//
// UI-004 / section 11.7: the browser is given the truth and the estimates on
// two separate paths. `getFrame` returns the truth ONLY as a display series
// tagged as simulation truth; it never enters an estimator. The estimators in
// the wasm build are the same objects as in the native build and reach the
// truth exactly as often as they do there, which is never.

#include <emscripten/bind.h>

#include <string>

#include "aerolab/build_info_shim.hpp"
#include "aerolab/io/runner.hpp"
#include "aerolab/io/scenario.hpp"

using namespace aerolab;  // NOLINT - binding translation unit

namespace {

// API-003: every failure crosses the boundary as a JSON object with a stable
// shape, never as an exception or an empty string.
std::string errorJson(const std::string& message) {
  Json j = Json::object();
  j["ok"] = Json(false);
  j["error"] = Json(message);
  return j.dump(0);
}

std::string okJson() {
  Json j = Json::object();
  j["ok"] = Json(true);
  return j.dump(0);
}

}  // namespace

class AerolabSession {
 public:
  // API-001. The browser fetches the scenario and configuration text and hands
  // them over; there is no filesystem in the wasm build.
  std::string loadScenario(const std::string& scenario_text, const std::string& label,
                           const std::string& config_text) {
    std::string error;
    Scenario scenario;
    if (!loadScenarioFromText(scenario_text, label, scenario, error)) return errorJson(error);
    if (!config_text.empty() && !applyConfigText(config_text, "config", scenario, error)) {
      return errorJson(error);
    }
    scenario_ = scenario;
    loaded_ = true;
    return reset(static_cast<double>(scenario_.seed));
  }

  std::string reset(double seed) {
    if (!loaded_) return errorJson("no scenario has been loaded");
    RunOptions options;
    options.has_seed_override = true;
    options.seed_override = static_cast<std::uint64_t>(seed);
    options.measure_tick_time = false;
    options.telemetry_path.clear();  // no filesystem in the browser

    runner_.reset(new SimulationRunner());
    std::string error;
    if (!runner_->configure(scenario_, options, error)) {
      runner_.reset();
      return errorJson(error);
    }
    if (!runner_->beginStreaming(error)) {
      runner_.reset();
      return errorJson(error);
    }
    finished_ = false;
    return okJson();
  }

  // Advances the core by `ticks` steps. The UI renders at 60 Hz while the core
  // runs at 100 Hz, so the page asks for however many ticks the elapsed frame
  // time is worth instead of coupling the two rates.
  bool step(int ticks) {
    if (!runner_ || finished_) return false;
    for (int i = 0; i < ticks; ++i) {
      if (!runner_->stepOnce()) {
        finished_ = true;
        return false;
      }
      if (runner_->finished()) {
        finished_ = true;
        break;
      }
    }
    return true;
  }

  std::string frame() {
    if (!runner_) return errorJson("no run in progress");
    return runner_->currentFrame().dump(0);
  }

  bool finished() const { return finished_; }

  // The run report: manifest, metrics and verdict, exactly as the native CLI
  // writes it to disk (UI-023).
  std::string report() {
    if (!runner_) return errorJson("no run in progress");
    const RunResult result = runner_->finishStreaming();
    finished_ = true;
    Json j = result.manifest;
    j["ok"] = Json(true);
    return j.dump(2);
  }

  // Static description of the loaded scenario, for the scenario selector and
  // the methodology panel.
  std::string scenarioInfo() const {
    if (!loaded_) return errorJson("no scenario has been loaded");
    Json j = scenarioToJson(scenario_);
    j["ok"] = Json(true);
    j["fault_start_s"] = Json(scenario_.faultWindowStart_s());
    j["fault_end_s"] = Json(scenario_.faultWindowEnd_s());
    Json acceptance = Json::array();
    for (const AcceptanceCriterion& a : scenario_.acceptance) {
      Json c = Json::object();
      c["id"] = Json(a.id);
      c["estimator"] = Json(a.estimator);
      c["description"] = Json(a.description);
      acceptance.push(c);
    }
    j["acceptance"] = acceptance;
    return j.dump(0);
  }

  static std::string buildInfo() {
    Json j = Json::object();
    j["ok"] = Json(true);
    j["version"] = Json(buildVersion());
    j["commit"] = Json(buildGitCommit());
    j["build_type"] = Json(buildType());
    j["compiler"] = Json(std::string(buildCompiler()) + " " + buildCompilerVersion());
    j["telemetry_schema"] = Json(kTelemetrySchemaVersion);
    j["scenario_schema"] = Json(kScenarioSchemaVersion);
    return j.dump(0);
  }

 private:
  Scenario scenario_{};
  std::unique_ptr<SimulationRunner> runner_;
  bool loaded_{false};
  bool finished_{false};
};

EMSCRIPTEN_BINDINGS(aerolab) {
  emscripten::class_<AerolabSession>("AerolabSession")
      .constructor<>()
      .function("loadScenario", &AerolabSession::loadScenario)
      .function("reset", &AerolabSession::reset)
      .function("step", &AerolabSession::step)
      .function("frame", &AerolabSession::frame)
      .function("finished", &AerolabSession::finished)
      .function("report", &AerolabSession::report)
      .function("scenarioInfo", &AerolabSession::scenarioInfo)
      .class_function("buildInfo", &AerolabSession::buildInfo);
}
