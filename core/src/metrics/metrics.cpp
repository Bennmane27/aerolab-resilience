#include "aerolab/metrics/metrics.hpp"

#include <algorithm>
#include <cmath>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/resource.h>
#endif

namespace aerolab {

Distribution Distribution::from(std::vector<double> samples) {
  Distribution d;
  d.count = samples.size();
  if (samples.empty()) return d;
  std::sort(samples.begin(), samples.end());
  double sum = 0.0;
  for (double v : samples) sum += v;
  d.mean = sum / static_cast<double>(samples.size());
  d.min = samples.front();
  d.max = samples.back();
  const std::size_t n = samples.size();
  d.median = (n % 2 == 1) ? samples[n / 2] : 0.5 * (samples[n / 2 - 1] + samples[n / 2]);
  // Nearest-rank P95, the convention stated in docs/methodology/metrics.md.
  const std::size_t rank = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(n)));
  d.p95 = samples[(rank == 0 ? 0 : rank - 1)];
  return d;
}

void MetricsAccumulator::reset(double fault_start_s, double fault_end_s,
                               const std::vector<SensorId>& faulted_sensors) {
  summary_ = MetricsSummary{};
  position_errors_m_.clear();
  horizontal_errors_m_.clear();
  tick_times_ms_.clear();
  sum_sq_position_ = 0.0;
  sum_sq_horizontal_ = 0.0;
  sum_sq_vertical_ = 0.0;
  samples_ = 0;
  fault_start_s_ = fault_start_s;
  fault_end_s_ = fault_end_s;
  faulted_sensors_ = faulted_sensors;
  summary_.fault_present = fault_start_s >= 0.0;
  usable_time_s_ = 0.0;
  last_sample_t_s_ = -1.0;
  was_usable_ = true;
  nis_normalised_sum_ = 0.0;
  nis_within_gate_ = 0;
  last_error_before_fault_end_m_ = -1.0;
  hasher_ = DeterminismHasher{};
}

void MetricsAccumulator::sample(double t_s, const TruthState& truth, const NavSolution& solution) {
  const Vec3 e = solution.position_ned_m - truth.position_ned_m;
  const double horizontal = std::sqrt(e.x * e.x + e.y * e.y);
  const double total = e.norm();

  position_errors_m_.push_back(total);
  horizontal_errors_m_.push_back(horizontal);
  sum_sq_position_ += total * total;
  sum_sq_horizontal_ += horizontal * horizontal;
  sum_sq_vertical_ += e.z * e.z;
  ++samples_;

  if (insideFault(t_s)) {
    if (total > summary_.max_error_during_fault_m) summary_.max_error_during_fault_m = total;
    last_error_before_fault_end_m_ = total;
  }

  // M-09 / M-10. "Usable" means the solution claims a mode a consumer could
  // act on. LOW_CONFIDENCE and UNSAFE are honest refusals and count as
  // unavailable, which is the point: refusing is better than lying, but it is
  // still an outage and the metric must say so.
  const bool usable = solution.valid && solution.mode != NavMode::kUnsafe &&
                      solution.mode != NavMode::kLowConfidence &&
                      solution.mode != NavMode::kInitializing;
  if (last_sample_t_s_ >= 0.0) {
    const double dt = t_s - last_sample_t_s_;
    if (usable) usable_time_s_ += dt;
  }
  if (was_usable_ && !usable) ++summary_.interruption_count;
  was_usable_ = usable;
  last_sample_t_s_ = t_s;

  // M-15: the replay-relevant output. Truth is included so a change in the
  // trajectory generator cannot pass unnoticed behind an unchanged estimator.
  hasher_.feed(t_s);
  hasher_.feed(truth.position_ned_m.x);
  hasher_.feed(truth.position_ned_m.y);
  hasher_.feed(truth.position_ned_m.z);
  hasher_.feed(solution.position_ned_m.x);
  hasher_.feed(solution.position_ned_m.y);
  hasher_.feed(solution.position_ned_m.z);
  hasher_.feed(static_cast<std::int64_t>(solution.mode));
}

void MetricsAccumulator::noteIntegrityEvent(const IntegrityEvent& e) {
  const bool inside = insideFault(e.t_s);
  // A detection is any departure from ACTIVE. Counting only ACTIVE -> SUSPECT
  // would score a source that vanished outright (ACTIVE -> UNAVAILABLE) as
  // "never detected", which is exactly backwards: a total loss is the most
  // unambiguous detection there is. SCN-002 and SCN-011 are the cases that
  // exposed this.
  // Detection is credited only for a source the scenario actually faulted.
  bool on_faulted_source = false;
  for (SensorId id : faulted_sensors_) {
    if (id == e.sensor) on_faulted_source = true;
  }
  const bool detection =
      on_faulted_source && e.from == SensorState::kActive && e.to != SensorState::kActive;
  const bool isolation = on_faulted_source && e.to == SensorState::kIsolated;

  if (inside || (fault_start_s_ >= 0.0 && e.t_s >= fault_start_s_)) {
    if (detection && summary_.time_to_detect_s < 0.0) {
      summary_.time_to_detect_s = e.t_s - fault_start_s_;  // M-05
      summary_.fault_detected = true;
    }
    if (isolation && summary_.time_to_isolate_s < 0.0) {
      summary_.time_to_isolate_s = e.t_s - fault_start_s_;  // M-06
      summary_.fault_isolated = true;
      summary_.fault_detected = true;
    }
  }
  // M-11: back to ACTIVE after the fault window closed.
  if (e.to == SensorState::kActive && fault_end_s_ >= 0.0 && e.t_s >= fault_end_s_ &&
      summary_.recovery_time_s < 0.0 &&
      (e.from == SensorState::kIsolated || e.from == SensorState::kUnavailable)) {
    summary_.recovery_time_s = e.t_s - fault_end_s_;
  }
  // M-07: an alert raised where no fault was active is a false alert, whether
  // the run is nominal or the alert simply landed outside the fault window.
  //
  // Only policy decisions count. A source going UNAVAILABLE is an observation,
  // not a claim: on the approach profiles the vision sensor legitimately loses
  // sight of the runway once the aircraft has rolled past it, and scoring that
  // as a false alert would put a geometric fact on the integrity budget.
  // An alert is raised once, when the source first leaves ACTIVE. Escalating
  // from SUSPECT to ISOLATED is the same alert getting worse, not a second one,
  // so it must not be double counted in the rate.
  const bool policy_alert = (e.from == SensorState::kActive &&
                             (e.to == SensorState::kSuspect || e.to == SensorState::kIsolated));
  if (!inside) {
    if (policy_alert) ++summary_.false_alert_count;
    if (isolation) ++summary_.false_isolation_count;
  }
}

void MetricsAccumulator::noteNis(double nis, int dof, double threshold) {
  if (dof <= 0 || !std::isfinite(nis)) return;
  nis_normalised_sum_ += nis / static_cast<double>(dof);
  if (nis <= threshold) ++nis_within_gate_;
  ++summary_.nis_samples;
}

MetricsSummary MetricsAccumulator::finalize(double total_time_s) {
  if (samples_ > 0) {
    const double n = static_cast<double>(samples_);
    summary_.position_rmse_m = std::sqrt(sum_sq_position_ / n);
    summary_.horizontal_rmse_m = std::sqrt(sum_sq_horizontal_ / n);
    summary_.vertical_rmse_m = std::sqrt(sum_sq_vertical_ / n);
  }
  summary_.position_error_m = Distribution::from(position_errors_m_);
  summary_.horizontal_error_m = Distribution::from(horizontal_errors_m_);
  summary_.tick_time_ms = Distribution::from(tick_times_ms_);

  summary_.availability = total_time_s > 0.0 ? usable_time_s_ / total_time_s : 0.0;
  // M-10. Continuity is binary per run, following the operational definition:
  // the service either was provided without unscheduled interruption for the
  // whole run, or it was not. The campaign-level continuity is the fraction of
  // runs scoring 1, which is what BEN-008 aggregates. Reporting a per-run
  // fraction here instead would blur a single 30 s outage into "97 % continuous"
  // and hide precisely the event that matters.
  summary_.continuity = summary_.interruption_count == 0 ? 1.0 : 0.0;

  summary_.false_alert_rate = summary_.gate_evaluations > 0
                                  ? static_cast<double>(summary_.false_isolation_count) /
                                        static_cast<double>(summary_.gate_evaluations)
                                  : 0.0;

  if (summary_.nis_samples > 0) {
    const double n = static_cast<double>(summary_.nis_samples);
    summary_.nis_mean_normalised = nis_normalised_sum_ / n;
    summary_.nis_fraction_within_gate = static_cast<double>(nis_within_gate_) / n;
  }
  summary_.error_at_fault_end_m = last_error_before_fault_end_m_;
  summary_.peak_memory_mb = peakResidentMemoryMb();
  summary_.determinism_hash = hasher_.hexDigest();
  return summary_;
}

double peakResidentMemoryMb() {
#if defined(__linux__)
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    return static_cast<double>(usage.ru_maxrss) / 1024.0;  // Linux reports KiB
  }
  return 0.0;
#elif defined(__APPLE__)
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    return static_cast<double>(usage.ru_maxrss) / (1024.0 * 1024.0);  // macOS reports bytes
  }
  return 0.0;
#else
  return 0.0;
#endif
}

}  // namespace aerolab
