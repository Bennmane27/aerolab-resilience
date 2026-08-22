// AEROLAB RESILIENCE - simulation runner. Owns the tick loop and the manifest.
//
// Requirements: SYS-001..SYS-017, BEN-011, BEN-014, DATA-004, DATA-005.
//
// Tick order (this is the corrected order of section 5.2, see DEV-004):
//   1. truth      = truth.sampleAt(t)                      analytic, stateless
//   2. staging    = sensors.sample(truth, t, dt)           nominal measurements
//   3. faults.apply(staging, t, phase)                     never sees the truth
//   4. bus.push(staging); due = bus.popDue(t)              delivery ordered
//   5. for each channel, for each due measurement:
//        IMU        -> estimator.consumeImu()
//        otherwise  -> prepareUpdate()  [no state change]
//                      integrity.evaluate()                <-- the gate
//                      applyUpdate()    [only if accepted]
//   6. solution separation test, navigation mode, metrics, telemetry
//
// Steps 1 to 4 happen ONCE per tick and every channel is fed the identical
// measurement sequence (BEN-002). That is what makes the comparison between
// architectures attributable to the architecture.
#pragma once

#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "aerolab/faults/fault_engine.hpp"
#include "aerolab/integrity/integrity_manager.hpp"
#include "aerolab/integrity/raim.hpp"
#include "aerolab/io/json.hpp"
#include "aerolab/io/scenario.hpp"
#include "aerolab/metrics/metrics.hpp"
#include "aerolab/navigation/estimator.hpp"
#include "aerolab/sensors/sensor_suite.hpp"
#include "aerolab/truth/ground_truth.hpp"

namespace aerolab {

struct RunOptions {
  bool has_seed_override{false};
  std::uint64_t seed_override{0};
  std::string telemetry_path;   // empty disables telemetry recording
  int telemetry_decimation{5};  // record one frame in N (DATA-011 promoted)
  bool measure_tick_time{true};
  bool verbose{false};
};

struct ChannelResult {
  EstimatorId estimator{EstimatorId::kEkf};
  MetricsSummary metrics{};
  std::vector<IntegrityEvent> events;
  NavMode final_mode{NavMode::kInitializing};
  SensorState final_gnss_state{SensorState::kActive};
  bool healthy{true};
  std::string diagnostic;
  double final_error_m{0.0};
};

struct RunResult {
  bool completed{false};
  std::string error;
  std::string scenario_id;
  std::uint64_t seed{0};
  double fault_start_s{-1.0};
  double fault_end_s{-1.0};
  double wall_time_s{0.0};
  std::vector<ChannelResult> channels;
  std::vector<FaultEvent> fault_events;
  RaimResult last_raim{};
  bool verdict_pass{true};
  std::vector<std::string> verdict_failures;
  Json manifest;

  const ChannelResult* channel(EstimatorId id) const {
    for (const ChannelResult& c : channels) {
      if (c.estimator == id) return &c;
    }
    return nullptr;
  }
};

class SimulationRunner {
 public:
  bool configure(const Scenario& scenario, const RunOptions& options, std::string& error);
  RunResult run();

  // Exposed for the WebAssembly binding, which drives the loop one tick at a
  // time from the browser render loop instead of running to completion.
  bool beginStreaming(std::string& error);
  bool stepOnce();
  // Not const: emitting a frame DRAINS the events accumulated since the last
  // frame was emitted. Reporting only the events of the current tick silently
  // dropped four out of five of them at a telemetry decimation of 5, and most
  // of them in the browser at x4 speed, because a rendered frame covers several
  // core ticks.
  Json currentFrame();
  bool finished() const { return tick_ > total_ticks_; }
  RunResult finishStreaming();

 private:
  struct Channel {
    NavigationChannelSpec spec;
    std::unique_ptr<INavigationEstimator> estimator;
    IntegrityManager integrity;
    MetricsAccumulator metrics;
    std::vector<IntegrityEvent> events;
    NavMode mode{NavMode::kInitializing};
    double error_m{0.0};
  };

  void buildChannels();
  NavSolution buildSeedSolution(const TruthState& truth0);
  void processTick();
  void evaluateVerdict(RunResult& result) const;
  Json buildManifest(const RunResult& result) const;

  Scenario scenario_{};
  RunOptions options_{};
  std::uint64_t seed_{0};

  GroundTruthSimulator truth_{};
  SensorSuite sensors_{};
  FaultInjectionEngine faults_{};
  MeasurementBus bus_{};
  RaimMonitor raim_{};
  std::vector<Channel> channels_;

  std::vector<Measurement> staging_;
  std::vector<Measurement> due_;
  std::vector<FaultEvent> fault_events_;
  // Drained by currentFrame(); see the comment on that method.
  std::vector<std::pair<EstimatorId, IntegrityEvent>> pending_integrity_events_;
  std::vector<FaultEvent> pending_fault_events_;
  std::vector<double> pseudorange_epoch_;
  double pseudorange_epoch_time_s_{-1.0};
  RaimResult last_raim_{};

  TruthState truth_state_{};
  std::int64_t tick_{0};
  std::int64_t total_ticks_{0};
  double t_s_{0.0};
  double wall_start_s_{0.0};
  double accumulated_wall_s_{0.0};

  // Held open for the whole run. Reopening per frame turned a 12 000 frame
  // trace into minutes of filesystem calls.
  std::ofstream telemetry_stream_;
  bool telemetry_open_{false};
  std::string telemetry_path_;
};

}  // namespace aerolab
