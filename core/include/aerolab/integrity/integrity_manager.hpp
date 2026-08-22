// AEROLAB RESILIENCE - integrity manager (subsystem S5).
//
// Requirements: INT-001..INT-022, UI-017, M-05..M-08.
//
// The manager is deliberately NOT part of the estimator (section 11.5). It sees
// only what prepareUpdate() produced - innovation, innovation covariance, NIS,
// measurement age - and answers one question: may this measurement be fused?
// That separation is what allows the benchmark to run the same EKF under
// several integrity policies and attribute the difference to the policy alone.
//
// State machine (INT-005..INT-009)
//   ACTIVE    -> SUSPECT     NIS above gate on N consecutive updates
//   SUSPECT   -> ISOLATED    persistence window elapsed, or a second check
//                            (velocity consistency, cross check, separation)
//                            confirms
//   SUSPECT   -> ACTIVE      residuals normal for clear_time_s
//   ISOLATED  -> ACTIVE      recovery window elapsed AND residuals normal
//   any       -> UNAVAILABLE the source stopped delivering
//   UNAVAILABLE -> ACTIVE    delivery resumed (through the recovery window)
//
// ISOLATED and UNAVAILABLE are distinct on purpose (INT-009): the first is a
// decision this software made and must justify, the second is an observation.
// Reporting one as the other would hide the only thing worth auditing.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "aerolab/core/types.hpp"
#include "aerolab/navigation/estimator.hpp"
#include "aerolab/sensors/measurement.hpp"
#include "aerolab/truth/truth_state.hpp"

namespace aerolab {

struct IntegrityConfig {
  // Fully disables the policy. Used by NAV-A and NAV-B, which have no integrity
  // architecture at all: giving them a monitor-only manager would emit SUSPECT
  // transitions and make the baselines look as if they detected something.
  bool enabled{true};

  // INT-010: when true the manager computes and journals everything but never
  // refuses a measurement. This is how NAV-C is run: identical instrumentation,
  // no gating, so the benchmark isolates the effect of the policy.
  bool monitor_only{false};

  // INT-004: gate expressed as a probability of false alert per update. The
  // chi-square threshold is derived per degree of freedom at configure time.
  double gate_probability_false_alert{1.0e-3};
  // Optional explicit override, used by the pedagogical mode of section 6.6.
  double explicit_gate_threshold{-1.0};

  int suspect_persistence_updates{3};  // INT-006
  double isolate_persistence_s{1.20};  // INT-006
  double clear_time_s{2.0};            // INT-005 back to ACTIVE
  double recovery_window_s{6.0};       // INT-007
  double stale_timeout_s{1.0};         // INT-018
  double unavailable_timeout_s{2.0};

  // INT-016: an explicit velocity consistency test, separate from the position
  // NIS, so SCN-005 gets its own reason code rather than a generic one.
  bool enable_velocity_consistency{true};
  double velocity_gate_probability_false_alert{1.0e-3};

  // INT-017: GNSS against the vision-derived position, only where vision is
  // available and its quality is above the floor.
  bool enable_vision_cross_check{true};
  double vision_cross_check_threshold_m{25.0};
  double vision_min_quality{0.20};

  // DEV-005: solution separation. Off for NAV-D (innovation gating only), on
  // for NAV-F, which is the comparison the headline result is built on.
  bool enable_solution_separation{false};
  double solution_separation_probability_false_alert{1.0e-5};
  // Inflation applied to (P_sub - P_full) before the separation test. See
  // SolutionSeparationEstimator::horizontalSeparation for why the theoretical
  // covariance is optimistic in practice.
  double solution_separation_covariance_inflation{1.0};
  // The separation test is evaluated when a GNSS position update is processed,
  // not on every tick. Running it at 100 Hz on a state that changes at 5 Hz
  // multiplies the number of (highly correlated) opportunities to cross the
  // threshold without adding any information.
  bool solution_separation_on_gnss_update_only{true};

  // INT-013: below this many usable absolute position sources the solution is
  // declared LOW_CONFIDENCE; with none at all and no recent fix, UNSAFE.
  int min_absolute_sources_for_normal{1};
  double low_confidence_fix_age_s{6.0};
  double unsafe_fix_age_s{20.0};
};

// INT-008 / INT-012: a transition is never emitted without the statistic, the
// threshold and the instant that produced it.
struct IntegrityEvent {
  double t_s{0.0};
  SensorId sensor{SensorId::kGnss};
  IntegrityReason reason{IntegrityReason::kNone};
  SensorState from{SensorState::kActive};
  SensorState to{SensorState::kActive};
  double statistic{0.0};
  double threshold{0.0};
};

struct SourceStatus {
  SensorState state{SensorState::kActive};
  double last_measurement_time_s{-1.0};
  double last_sample_time_s{-1.0};
  double age_s{0.0};
  double last_nis{0.0};
  double last_threshold{0.0};
  int consecutive_exceedances{0};
  double suspect_since_s{-1.0};
  double isolated_since_s{-1.0};
  double last_normal_s{0.0};
  double quality{1.0};
  IntegrityReason last_reason{IntegrityReason::kNone};
  bool ever_seen{false};
};

struct IntegrityDecision {
  bool accept{true};
  IntegrityReason reason{IntegrityReason::kNone};
  double statistic{0.0};
  double threshold{0.0};
};

class IntegrityManager {
 public:
  void configure(const IntegrityConfig& config, const RunwayScene& scene);
  void reset();

  // Called once per tick before any measurement, to age out sources.
  void beginTick(double now_s);

  // The gate. Called between prepareUpdate() and applyUpdate().
  IntegrityDecision evaluate(const Measurement& m, const InnovationInfo& info, double now_s);

  // Second-line check fed by NAV-F (DEV-005). `statistic` is the separation
  // chi-square; the manager owns the threshold and the state machine so both
  // policies share exactly the same isolation logic.
  void submitSolutionSeparation(double statistic, double separation_m, int dof, double now_s);

  // INT-013 / INT-014: the mode the solution is entitled to claim.
  NavMode navigationMode(double now_s, double last_absolute_fix_age_s) const;

  SensorState stateOf(SensorId id) const { return status_[index(id)].state; }
  const SourceStatus& statusOf(SensorId id) const { return status_[index(id)]; }
  const std::vector<IntegrityEvent>& events() const { return events_; }
  void clearEvents() { events_.clear(); }

  // UI trust score (INT-015 / section 6.7). Explicitly NOT used by any gating
  // decision: it is a presentation artefact derived from published metrics.
  double trustScore(SensorId id, double now_s) const;

  double gateThresholdFor(int dof) const;
  double separationThreshold() const { return separation_threshold_; }
  double lastSeparationStatistic() const { return last_separation_statistic_; }
  double lastSeparation_m() const { return last_separation_m_; }

  int activeAbsoluteSourceCount() const;

 private:
  static std::size_t index(SensorId id) { return static_cast<std::size_t>(id); }

  void transition(SourceStatus& s, SensorId id, SensorState to, IntegrityReason reason,
                  double now_s, double statistic, double threshold);
  void noteNormal(SourceStatus& s, SensorId id, double now_s);
  void noteExceedance(SourceStatus& s, SensorId id, double now_s, double statistic,
                      double threshold, IntegrityReason reason);

  IntegrityConfig config_{};
  RunwayScene scene_{};
  std::array<SourceStatus, static_cast<std::size_t>(SensorId::kCount)> status_{};
  std::vector<IntegrityEvent> events_;

  std::array<double, 7> gate_by_dof_{};  // index = dof, 1..6
  double velocity_gate_dof3_{0.0};
  double separation_threshold_{0.0};
  double last_separation_statistic_{0.0};
  double last_separation_m_{0.0};

  // INT-017 cross-check memory.
  bool has_gnss_fix_{false};
  Vec3 last_gnss_position_ned_m_{};
  double last_gnss_position_time_s_{-1.0};
  bool has_vision_fix_{false};
  Vec3 last_vision_position_ned_m_{};
  double last_vision_time_s_{-1.0};
  double last_vision_quality_{0.0};
};

}  // namespace aerolab
