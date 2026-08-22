#include "aerolab/integrity/integrity_manager.hpp"

#include <cmath>

#include "aerolab/integrity/chi_square.hpp"

namespace aerolab {

void IntegrityManager::configure(const IntegrityConfig& config, const RunwayScene& scene) {
  config_ = config;
  scene_ = scene;
  for (int dof = 1; dof <= 6; ++dof) {
    gate_by_dof_[static_cast<std::size_t>(dof)] =
        config_.explicit_gate_threshold > 0.0
            ? config_.explicit_gate_threshold
            : gateThreshold(config_.gate_probability_false_alert, dof);
  }
  velocity_gate_dof3_ = gateThreshold(config_.velocity_gate_probability_false_alert, 3);
  separation_threshold_ = gateThreshold(config_.solution_separation_probability_false_alert, 2);
  reset();
}

void IntegrityManager::reset() {
  for (SourceStatus& s : status_) s = SourceStatus{};
  events_.clear();
  last_separation_statistic_ = 0.0;
  last_separation_m_ = 0.0;
  has_gnss_fix_ = false;
  has_vision_fix_ = false;
  last_gnss_position_time_s_ = -1.0;
  last_vision_time_s_ = -1.0;
  last_vision_quality_ = 0.0;
}

double IntegrityManager::gateThresholdFor(int dof) const {
  if (dof < 1) dof = 1;
  if (dof > 6) dof = 6;
  return gate_by_dof_[static_cast<std::size_t>(dof)];
}

void IntegrityManager::transition(SourceStatus& s, SensorId id, SensorState to,
                                  IntegrityReason reason, double now_s, double statistic,
                                  double threshold) {
  if (s.state == to) return;
  IntegrityEvent e;
  e.t_s = now_s;
  e.sensor = id;
  e.reason = reason;
  e.from = s.state;
  e.to = to;
  e.statistic = statistic;
  e.threshold = threshold;
  events_.push_back(e);
  s.state = to;
  s.last_reason = reason;
  if (to == SensorState::kSuspect) {
    s.suspect_since_s = now_s;
  } else if (to == SensorState::kIsolated) {
    s.isolated_since_s = now_s;
  } else if (to == SensorState::kActive) {
    s.suspect_since_s = -1.0;
    s.isolated_since_s = -1.0;
    s.consecutive_exceedances = 0;
  }
}

void IntegrityManager::noteNormal(SourceStatus& s, SensorId id, double now_s) {
  s.consecutive_exceedances = 0;
  if (s.state == SensorState::kActive) {
    s.last_normal_s = now_s;
    return;
  }
  if (s.state == SensorState::kSuspect) {
    // INT-005: a source only clears after sustained normality, not on the first
    // good sample. Otherwise an intermittent fault oscillates forever.
    if (now_s - s.last_normal_s >= config_.clear_time_s && s.last_normal_s > 0.0) {
      transition(s, id, SensorState::kActive, IntegrityReason::kNisNormalCleared, now_s, s.last_nis,
                 s.last_threshold);
    }
    if (s.last_normal_s <= 0.0) s.last_normal_s = now_s;
    return;
  }
  if (s.state == SensorState::kIsolated) {
    // INT-007: the recovery window is measured from the isolation instant and
    // requires normal residuals throughout, not merely elapsed time.
    if (s.isolated_since_s >= 0.0 && now_s - s.isolated_since_s >= config_.recovery_window_s) {
      transition(s, id, SensorState::kActive, IntegrityReason::kRecoveryWindowElapsed, now_s,
                 s.last_nis, s.last_threshold);
    }
    return;
  }
  if (s.state == SensorState::kUnavailable) {
    transition(s, id, SensorState::kActive, IntegrityReason::kSourceReturned, now_s, 0.0, 0.0);
  }
}

void IntegrityManager::noteExceedance(SourceStatus& s, SensorId id, double now_s, double statistic,
                                      double threshold, IntegrityReason reason) {
  ++s.consecutive_exceedances;
  s.last_normal_s = 0.0;
  if (s.state == SensorState::kActive &&
      s.consecutive_exceedances >= config_.suspect_persistence_updates) {
    transition(s, id, SensorState::kSuspect, reason, now_s, statistic, threshold);
    return;
  }
  if (s.state == SensorState::kSuspect && s.suspect_since_s >= 0.0 &&
      now_s - s.suspect_since_s >= config_.isolate_persistence_s) {
    // INT-008: the reason must explain what actually escalated. Hard coding
    // NIS_PERSISTENT here labelled every isolation as an innovation failure,
    // including the ones driven by measurement age or a repeated sequence,
    // which is precisely the audit trail this project exists to get right.
    transition(s, id, SensorState::kIsolated, reason, now_s, statistic, threshold);
  }
}

void IntegrityManager::beginTick(double now_s) {
  for (std::size_t i = 0; i < status_.size(); ++i) {
    SourceStatus& s = status_[i];
    if (!s.ever_seen) continue;
    const SensorId id = static_cast<SensorId>(i);
    s.age_s = now_s - s.last_sample_time_s;
    if (s.state != SensorState::kUnavailable &&
        now_s - s.last_measurement_time_s > config_.unavailable_timeout_s) {
      transition(s, id, SensorState::kUnavailable, IntegrityReason::kSourceUnavailable, now_s,
                 s.age_s, config_.unavailable_timeout_s);
    }
  }
}

IntegrityDecision IntegrityManager::evaluate(const Measurement& m, const InnovationInfo& info,
                                             double now_s) {
  IntegrityDecision d;
  if (!config_.enabled) return d;  // no policy: everything is accepted as is
  SourceStatus& s = status_[index(m.header.sensor)];
  const SensorId id = m.header.sensor;
  s.ever_seen = true;
  s.quality = m.quality;

  // --- delivery level facts, independent of any filter ------------------------
  if (m.header.validity == Validity::kUnavailable) {
    transition(s, id, SensorState::kUnavailable, IntegrityReason::kSourceUnavailable, now_s, 0.0,
               0.0);
    d.accept = false;
    d.reason = IntegrityReason::kSourceUnavailable;
    return d;
  }
  if (m.header.validity == Validity::kInvalidNumeric || m.header.validity == Validity::kDropped) {
    d.accept = false;
    d.reason = m.header.validity == Validity::kInvalidNumeric
                   ? IntegrityReason::kInnovationCovarianceInvalid
                   : IntegrityReason::kSourceUnavailable;
    return d;
  }

  // INT-019: a repeated sequence number means the source is replaying itself.
  const bool repeated_sequence = s.ever_seen && s.last_measurement_time_s >= 0.0 &&
                                 m.header.sample_time_s <= s.last_sample_time_s &&
                                 m.header.sample_time_s > 0.0;
  s.last_measurement_time_s = now_s;

  // INT-018: age is computed from sample_time, which no fault is allowed to
  // rewrite, so a frozen or delayed source cannot hide behind a fresh header.
  const double age = now_s - m.header.sample_time_s;
  s.age_s = age;

  if (repeated_sequence && age > config_.stale_timeout_s) {
    noteExceedance(s, id, now_s, age, config_.stale_timeout_s, IntegrityReason::kSequenceRepeated);
    if (s.state == SensorState::kActive) {
      transition(s, id, SensorState::kSuspect, IntegrityReason::kSequenceRepeated, now_s, age,
                 config_.stale_timeout_s);
    }
    d.accept = config_.monitor_only;
    d.reason = IntegrityReason::kSequenceRepeated;
    d.statistic = age;
    d.threshold = config_.stale_timeout_s;
    if (!config_.monitor_only) return d;
  }
  if (age > config_.stale_timeout_s) {
    if (s.state == SensorState::kActive) {
      transition(s, id, SensorState::kSuspect, IntegrityReason::kMeasurementStale, now_s, age,
                 config_.stale_timeout_s);
    } else if (s.state == SensorState::kSuspect && s.suspect_since_s >= 0.0 &&
               now_s - s.suspect_since_s >= config_.isolate_persistence_s) {
      transition(s, id, SensorState::kIsolated, IntegrityReason::kMeasurementStale, now_s, age,
                 config_.stale_timeout_s);
    }
    d.accept = config_.monitor_only;
    d.reason = IntegrityReason::kMeasurementStale;
    d.statistic = age;
    d.threshold = config_.stale_timeout_s;
    if (!config_.monitor_only) return d;
  }
  if (m.header.sample_time_s > s.last_sample_time_s) s.last_sample_time_s = m.header.sample_time_s;

  // --- vision quality floor (SENS-018, SCN-010) -------------------------------
  //
  // A measurement whose quality is below the usable floor is refused, but the
  // source is marked UNAVAILABLE, not SUSPECT. The distinction matters: on every
  // approach the runway enters the sensor envelope at essentially zero quality
  // and improves as the range closes. Calling that SUSPECT would put a
  // geometric certainty on the false alert budget and would make the UI claim a
  // fault where there is none. UNAVAILABLE says what is actually true - there is
  // no usable measurement yet - and the source returns to ACTIVE by itself once
  // the quality comes up.
  if (m.type == MeasurementType::kVisionRelative && m.quality < config_.vision_min_quality) {
    if (s.state == SensorState::kActive || s.state == SensorState::kSuspect) {
      transition(s, id, SensorState::kUnavailable, IntegrityReason::kQualityBelowThreshold, now_s,
                 m.quality, config_.vision_min_quality);
    }
    d.accept = config_.monitor_only;
    d.reason = IntegrityReason::kQualityBelowThreshold;
    d.statistic = m.quality;
    d.threshold = config_.vision_min_quality;
    if (!config_.monitor_only) return d;
  }

  // --- filter level statistic -------------------------------------------------
  if (!info.valid) {
    d.accept = false;
    d.reason = info.rejection_hint != IntegrityReason::kNone
                   ? info.rejection_hint
                   : IntegrityReason::kInnovationCovarianceInvalid;
    return d;
  }

  const bool is_velocity = (m.type == MeasurementType::kGnssVelocity);
  const double threshold = is_velocity && config_.enable_velocity_consistency
                               ? velocity_gate_dof3_
                               : gateThresholdFor(info.dim);
  s.last_nis = info.nis;
  s.last_threshold = threshold;
  d.statistic = info.nis;
  d.threshold = threshold;

  const IntegrityReason exceed_reason =
      is_velocity ? IntegrityReason::kVelocityInconsistent
                  : (m.header.sensor == SensorId::kGnss ? IntegrityReason::kCrossCheckInertial
                                                        : IntegrityReason::kNisAboveThreshold);

  if (info.nis > threshold) {
    noteExceedance(s, id, now_s, info.nis, threshold, exceed_reason);
    d.reason = exceed_reason;
  } else {
    noteNormal(s, id, now_s);
  }

  // --- INT-017 GNSS versus vision cross check ---------------------------------
  if (m.type == MeasurementType::kGnssPosition) {
    has_gnss_fix_ = true;
    last_gnss_position_ned_m_ = Vec3(m.v[0], m.v[1], m.v[2]);
    last_gnss_position_time_s_ = m.header.sample_time_s;
  } else if (m.type == MeasurementType::kVisionRelative) {
    // Vision reports runway-frame offsets; converting them back to NED needs
    // only the static scene geometry.
    const double cpsi = std::cos(scene_.heading_rad);
    const double spsi = std::sin(scene_.heading_rad);
    last_vision_position_ned_m_ =
        Vec3(scene_.threshold_ned_m.x + m.v[1] * cpsi - m.v[0] * spsi,
             scene_.threshold_ned_m.y + m.v[1] * spsi + m.v[0] * cpsi, 0.0);
    last_vision_time_s_ = m.header.sample_time_s;
    last_vision_quality_ = m.quality;
    has_vision_fix_ = true;
  }
  if (config_.enable_vision_cross_check && has_gnss_fix_ && has_vision_fix_ &&
      last_vision_quality_ >= config_.vision_min_quality &&
      std::fabs(last_gnss_position_time_s_ - last_vision_time_s_) < 0.5 &&
      m.header.sensor == SensorId::kGnss) {
    const double dn = last_gnss_position_ned_m_.x - last_vision_position_ned_m_.x;
    const double de = last_gnss_position_ned_m_.y - last_vision_position_ned_m_.y;
    const double separation = std::sqrt(dn * dn + de * de);
    if (separation > config_.vision_cross_check_threshold_m) {
      noteExceedance(s, id, now_s, separation, config_.vision_cross_check_threshold_m,
                     IntegrityReason::kCrossCheckVision);
      d.reason = IntegrityReason::kCrossCheckVision;
      d.statistic = separation;
      d.threshold = config_.vision_cross_check_threshold_m;
    }
  }

  // INT-010 / INT-011: in monitor-only mode everything above still runs and is
  // journaled, but nothing is refused. Elsewhere, refusing a measurement never
  // stops the estimator - it just does not get that correction.
  if (config_.monitor_only) {
    d.accept = true;
    return d;
  }
  d.accept = (s.state == SensorState::kActive || s.state == SensorState::kSuspect);
  if (!d.accept && d.reason == IntegrityReason::kNone) {
    d.reason =
        s.last_reason != IntegrityReason::kNone ? s.last_reason : IntegrityReason::kNisPersistent;
  }
  return d;
}

void IntegrityManager::submitSolutionSeparation(double statistic, double separation_m, int dof,
                                                double now_s) {
  if (!config_.enable_solution_separation) return;
  last_separation_statistic_ = statistic;
  last_separation_m_ = separation_m;
  const double threshold =
      dof == 2 ? separation_threshold_
               : gateThreshold(config_.solution_separation_probability_false_alert, dof);
  SourceStatus& s = status_[index(SensorId::kGnss)];
  if (!s.ever_seen) return;
  if (s.state == SensorState::kIsolated || s.state == SensorState::kUnavailable) return;

  if (statistic > threshold) {
    // The separation test is the second line: it confirms straight to ISOLATED
    // once persistent, because unlike the innovation gate it does not lose
    // power as the fault grows.
    if (s.state == SensorState::kActive) {
      transition(s, SensorId::kGnss, SensorState::kSuspect, IntegrityReason::kSolutionSeparation,
                 now_s, statistic, threshold);
    } else if (s.state == SensorState::kSuspect && s.suspect_since_s >= 0.0 &&
               now_s - s.suspect_since_s >= config_.isolate_persistence_s) {
      transition(s, SensorId::kGnss, SensorState::kIsolated, IntegrityReason::kSolutionSeparation,
                 now_s, statistic, threshold);
    }
  }
}

int IntegrityManager::activeAbsoluteSourceCount() const {
  int n = 0;
  const SensorId absolute[] = {SensorId::kGnss, SensorId::kVision};
  for (SensorId id : absolute) {
    const SourceStatus& s = status_[index(id)];
    if (s.ever_seen && (s.state == SensorState::kActive || s.state == SensorState::kSuspect)) ++n;
  }
  return n;
}

NavMode IntegrityManager::navigationMode(double now_s, double last_absolute_fix_age_s) const {
  (void)now_s;
  if (!config_.enabled) return NavMode::kInitializing;  // estimator keeps its own mode
  const int absolute = activeAbsoluteSourceCount();
  // INT-014: the mode reflects what the policy can still support, never what
  // would look better on screen.
  if (last_absolute_fix_age_s > config_.unsafe_fix_age_s) return NavMode::kUnsafe;
  if (absolute == 0) {
    return last_absolute_fix_age_s > config_.low_confidence_fix_age_s ? NavMode::kLowConfidence
                                                                      : NavMode::kDeadReckoning;
  }
  if (absolute < config_.min_absolute_sources_for_normal) return NavMode::kLowConfidence;
  for (const SourceStatus& s : status_) {
    if (s.ever_seen &&
        (s.state == SensorState::kIsolated || s.state == SensorState::kUnavailable)) {
      return NavMode::kDegraded;
    }
  }
  return NavMode::kNormal;
}

double IntegrityManager::trustScore(SensorId id, double now_s) const {
  // Section 6.7 / INT-015. Presentation only. No branch in this file reads it.
  const SourceStatus& s = status_[index(id)];
  if (!s.ever_seen) return 0.0;
  switch (s.state) {
    case SensorState::kUnavailable: return 0.0;
    case SensorState::kIsolated: return 0.05;
    default: break;
  }
  const double freshness =
      1.0 - std::min(1.0, std::max(0.0, (now_s - s.last_sample_time_s) / config_.stale_timeout_s));
  const double threshold = s.last_threshold > 0.0 ? s.last_threshold : 1.0;
  const double consistency = 1.0 - std::min(1.0, s.last_nis / (2.0 * threshold));
  const double stability = s.state == SensorState::kSuspect ? 0.4 : 1.0;
  const double quality = std::min(1.0, std::max(0.0, s.quality));
  const double score = 0.30 * freshness + 0.35 * consistency + 0.20 * stability + 0.15 * quality;
  return std::min(1.0, std::max(0.0, score));
}

}  // namespace aerolab
