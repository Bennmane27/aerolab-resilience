// AEROLAB RESILIENCE - metrics M-01 to M-15 (subsystem S6 support).
//
// Requirements: M-01..M-15, BEN-003..BEN-010, BEN-016, section 8.1.
//
// Two rules from section 8.1 are enforced structurally rather than by
// convention:
//   * No mean is ever published alone. Every distribution reports median, P95
//     and worst case alongside the mean (BEN-016).
//   * A detection that never happened is recorded as a missed detection with an
//     infinite time to detect, not omitted from the average. Dropping the
//     failures is the single easiest way to publish a flattering benchmark, and
//     SCN-004 exists to produce exactly those cases.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "aerolab/core/hash.hpp"
#include "aerolab/integrity/integrity_manager.hpp"
#include "aerolab/navigation/estimator.hpp"
#include "aerolab/truth/truth_state.hpp"

namespace aerolab {

struct Distribution {
  double mean{0.0};
  double median{0.0};
  double p95{0.0};
  double max{0.0};
  double min{0.0};
  std::size_t count{0};

  static Distribution from(std::vector<double> samples);
};

struct MetricsSummary {
  // M-01..M-04
  double position_rmse_m{0.0};
  double horizontal_rmse_m{0.0};
  double vertical_rmse_m{0.0};
  Distribution position_error_m{};
  Distribution horizontal_error_m{};

  // M-05, M-06, M-11. Negative means "never happened"; the JSON writer emits
  // null for those so an aggregation cannot silently average them as zero.
  double time_to_detect_s{-1.0};
  double time_to_isolate_s{-1.0};
  double recovery_time_s{-1.0};

  // M-07, M-08
  std::size_t gate_evaluations{0};
  std::size_t false_alert_count{0};
  std::size_t false_isolation_count{0};
  double false_alert_rate{0.0};
  bool fault_present{false};
  bool fault_detected{false};
  bool fault_isolated{false};

  // M-09, M-10
  double availability{0.0};
  double continuity{0.0};
  std::size_t interruption_count{0};

  // M-12
  double nis_mean_normalised{0.0};  // E[NIS]/dof, 1.0 when the filter is consistent
  double nis_fraction_within_gate{0.0};
  std::size_t nis_samples{0};

  // M-13, M-14
  Distribution tick_time_ms{};
  double peak_memory_mb{0.0};

  // M-15
  std::string determinism_hash;

  // Error at the moment the fault window closed - the number that answers
  // "how far off were we by the time it was over".
  double error_at_fault_end_m{-1.0};
  double max_error_during_fault_m{0.0};
};

// One accumulator per navigation channel.
class MetricsAccumulator {
 public:
  // `faulted_sensors` is the set of sources the scenario actually injects a
  // fault into. A detection is only credited when the architecture flags ONE OF
  // THOSE. Crediting any departure from ACTIVE scored the vision sensor losing
  // sight of the runway at touchdown as a detection of a GNSS drift, which made
  // the detectability sweep report 100 % detection at every drift rate down to
  // 0.1 m/s - a result that was pure artefact.
  void reset(double fault_start_s, double fault_end_s,
             const std::vector<SensorId>& faulted_sensors = {});

  // Called once per tick, after every measurement of the tick has been handled.
  void sample(double t_s, const TruthState& truth, const NavSolution& solution);

  void noteGateEvaluation() { ++summary_.gate_evaluations; }

  void noteIntegrityEvent(const IntegrityEvent& e);

  void noteNis(double nis, int dof, double threshold);

  void noteTickTime(double milliseconds) { tick_times_ms_.push_back(milliseconds); }

  DeterminismHasher& hasher() { return hasher_; }

  MetricsSummary finalize(double total_time_s);

 private:
  bool insideFault(double t_s) const {
    return fault_start_s_ >= 0.0 && t_s >= fault_start_s_ && t_s <= fault_end_s_;
  }

  MetricsSummary summary_{};
  std::vector<double> position_errors_m_;
  std::vector<double> horizontal_errors_m_;
  std::vector<double> tick_times_ms_;
  double sum_sq_position_{0.0};
  double sum_sq_horizontal_{0.0};
  double sum_sq_vertical_{0.0};
  std::size_t samples_{0};

  double fault_start_s_{-1.0};
  double fault_end_s_{-1.0};
  std::vector<SensorId> faulted_sensors_;

  double usable_time_s_{0.0};
  double last_sample_t_s_{-1.0};
  bool was_usable_{true};

  double nis_normalised_sum_{0.0};
  std::size_t nis_within_gate_{0};

  double last_error_before_fault_end_m_{-1.0};

  DeterminismHasher hasher_;
};

// M-14: resident set size in MiB. Returns 0 where the platform does not expose
// it (the wasm build); the benchmark reports it as unavailable rather than as
// zero bytes used.
double peakResidentMemoryMb();

}  // namespace aerolab
