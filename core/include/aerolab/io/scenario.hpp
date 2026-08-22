// AEROLAB RESILIENCE - scenario definition and loader.
//
// Requirements: DATA-002, DATA-003, DATA-005, SYS-007, SYS-010, SYS-016,
// FI-019, FI-020.
//
// DEVIATION DEV-007 (docs/deviations.md) - machine readable acceptance block.
// SYS-016 requires a run to "export a verdict of conformance to the criteria of
// the suite", but section 7 only states the expected outcome of each scenario
// in prose, in the "Attendu" column. Prose is not computable, so every scenario
// file here carries an `acceptance:` block: a list of criteria, each bound to
// one estimator channel, each evaluated against the metrics of the run. The
// verdict in the manifest is the conjunction of those criteria.
//
// Design rule kept from section 8.1: a criterion states what the RUN must
// produce (finite metrics, journaled events, honest mode), never that a
// particular algorithm must win. SCN-004 is expected to defeat the innovation
// gate, and its acceptance block says so.
#pragma once

#include <string>
#include <vector>

#include "aerolab/faults/fault_engine.hpp"
#include "aerolab/integrity/integrity_manager.hpp"
#include "aerolab/io/json.hpp"
#include "aerolab/navigation/estimator.hpp"
#include "aerolab/sensors/sensor_suite.hpp"
#include "aerolab/truth/ground_truth.hpp"

namespace aerolab {

inline constexpr int kScenarioSchemaVersion = 1;
inline constexpr int kTelemetrySchemaVersion = 1;

struct AcceptanceCriterion {
  std::string id{"AC-1"};
  std::string description;
  // "any" applies the criterion to every channel of the run.
  std::string estimator{"any"};

  bool has_max_position_rmse{false};
  double max_position_rmse_m{0.0};
  bool has_max_error{false};
  double max_error_m{0.0};
  bool has_max_horizontal_p95{false};
  double max_horizontal_p95_m{0.0};
  bool require_detection{false};
  bool require_isolation{false};
  bool has_max_time_to_detect{false};
  double max_time_to_detect_s{0.0};
  bool has_max_time_to_isolate{false};
  double max_time_to_isolate_s{0.0};
  bool has_max_false_isolations{false};
  int max_false_isolations{0};
  bool has_min_availability{false};
  double min_availability{0.0};
  bool has_forbidden_end_mode{false};
  NavMode forbidden_end_mode{NavMode::kNormal};
  bool has_required_end_mode{false};
  NavMode required_end_mode{NavMode::kNormal};
};

struct NavigationChannelSpec {
  EstimatorId estimator{EstimatorId::kEkf};
  IntegrityConfig integrity{};
};

struct Scenario {
  int schema_version{kScenarioSchemaVersion};
  std::string id{"SCN-001"};
  std::string name;
  std::string description;
  std::string objective;

  std::uint64_t seed{1};
  double duration_s{90.0};
  double dt_s{0.01};

  TrajectoryProfile profile{};
  RunwayScene scene{};
  SensorSuiteConfig sensors{};
  EstimatorConfig estimator_config{};
  std::vector<FaultSpec> faults;
  std::vector<NavigationChannelSpec> channels;
  std::vector<AcceptanceCriterion> acceptance;

  // Content hashes of the files this scenario was built from (DATA-005).
  std::string scenario_hash;
  std::string config_hash;
  std::string source_path;

  // First fault start / last fault end, used as t0 for M-05, M-06 and M-11.
  double faultWindowStart_s() const;
  double faultWindowEnd_s() const;
  // The sources this scenario actually injects a fault into, used to attribute
  // a detection to the right thing (M-05, M-08).
  std::vector<SensorId> faultedSensors() const;
};

// Returns the default channel set: NAV-A, NAV-B, NAV-C (monitor only),
// NAV-D (innovation gating) and NAV-F (gating + solution separation).
std::vector<NavigationChannelSpec> defaultChannels();

// Builds the integrity configuration a given estimator id is run with. This is
// the single place where "which policy goes with which architecture" is
// decided, so the comparison stays honest across every scenario.
IntegrityConfig defaultIntegrityFor(EstimatorId id);

// SYS-010 / API-005: any problem is reported as a message, never as a partially
// populated scenario.
bool loadScenario(const std::string& path, Scenario& out, std::string& error);
bool loadScenarioFromJson(const Json& root, Scenario& out, std::string& error);

// Loads from an in-memory document. Used by the WebAssembly build, where there
// is no filesystem: the browser fetches the scenario text and hands it over.
// `label` becomes the recorded source path so the manifest still says where the
// scenario came from.
bool loadScenarioFromText(const std::string& text, const std::string& label, Scenario& out,
                          std::string& error);

// Applies an estimator/integrity configuration file on top of a scenario.
bool applyConfigFile(const std::string& path, Scenario& scenario, std::string& error);
bool applyConfigText(const std::string& text, const std::string& label, Scenario& scenario,
                     std::string& error);
void applyConfigJson(const Json& root, Scenario& scenario);

Json scenarioToJson(const Scenario& s);

}  // namespace aerolab
