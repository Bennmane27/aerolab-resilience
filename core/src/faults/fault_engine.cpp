#include "aerolab/faults/fault_engine.hpp"

#include <cmath>

namespace aerolab {

const char* toString(FaultType t) {
  switch (t) {
    case FaultType::kSourceUnavailable: return "source_unavailable";
    case FaultType::kFreeze: return "freeze";
    case FaultType::kBiasStep: return "bias_step";
    case FaultType::kBiasRamp: return "bias_ramp";
    case FaultType::kNoiseBurst: return "noise_burst";
    case FaultType::kLatency: return "latency";
    case FaultType::kDropProbabilistic: return "drop_probabilistic";
    case FaultType::kGnssPositionStep: return "gnss_position_step";
    case FaultType::kGnssPositionRamp: return "gnss_position_ramp";
    case FaultType::kGnssVelocityInconsistency: return "gnss_velocity_inconsistency";
    case FaultType::kImuAccelBias: return "imu_accel_bias";
    case FaultType::kImuGyroBias: return "imu_gyro_bias";
    case FaultType::kVisionDegrade: return "vision_degrade";
    case FaultType::kPseudorangeOutlier: return "pseudorange_outlier";
  }
  return "unknown";
}

bool parseFaultType(const std::string& name, FaultType& out) {
  static const struct {
    const char* n;
    FaultType t;
  } kTable[] = {
      {"source_unavailable", FaultType::kSourceUnavailable},
      {"freeze", FaultType::kFreeze},
      {"bias_step", FaultType::kBiasStep},
      {"bias_ramp", FaultType::kBiasRamp},
      {"noise_burst", FaultType::kNoiseBurst},
      {"latency", FaultType::kLatency},
      {"drop_probabilistic", FaultType::kDropProbabilistic},
      {"gnss_position_step", FaultType::kGnssPositionStep},
      {"gnss_position_ramp", FaultType::kGnssPositionRamp},
      {"gnss_velocity_inconsistency", FaultType::kGnssVelocityInconsistency},
      {"imu_accel_bias", FaultType::kImuAccelBias},
      {"imu_gyro_bias", FaultType::kImuGyroBias},
      {"vision_degrade", FaultType::kVisionDegrade},
      {"pseudorange_outlier", FaultType::kPseudorangeOutlier},
  };
  for (const auto& e : kTable) {
    if (name == e.n) {
      out = e.t;
      return true;
    }
  }
  return false;
}

bool FaultInjectionEngine::configure(const std::vector<FaultSpec>& specs, std::string& error) {
  error.clear();
  for (std::size_t i = 0; i < specs.size(); ++i) {
    const FaultSpec& s = specs[i];
    const std::string where = "fault '" + s.id + "': ";
    if (s.start_s < 0.0 && !s.use_phase_trigger) {
      error = where + "start_s must be >= 0";
      return false;
    }
    if (s.duration_s == 0.0) {
      error = where + "duration_s must be non-zero (use a negative value for open ended)";
      return false;
    }
    if (!std::isfinite(s.start_s) || !std::isfinite(s.duration_s)) {
      error = where + "non-finite start or duration";
      return false;
    }
    if (!s.amplitude.isFinite() || !std::isfinite(s.scalar)) {
      error = where + "non-finite amplitude";
      return false;
    }
    switch (s.type) {
      case FaultType::kDropProbabilistic:
        if (s.scalar < 0.0 || s.scalar > 1.0) {
          error = where + "drop probability must lie in [0, 1]";
          return false;
        }
        if (s.scalar_final >= 0.0 && s.scalar_final > 1.0) {
          error = where + "final drop probability must lie in [0, 1]";
          return false;
        }
        break;
      case FaultType::kVisionDegrade:
        if (s.scalar < 0.0 || s.scalar > 1.0) {
          error = where + "vision quality must lie in [0, 1]";
          return false;
        }
        if (s.target != SensorId::kVision) {
          error = where + "vision_degrade targets the vision sensor only";
          return false;
        }
        break;
      case FaultType::kLatency:
        if (s.scalar <= 0.0) {
          error = where + "latency must be strictly positive [s]";
          return false;
        }
        if (s.scalar > 30.0) {
          error = where + "latency above 30 s is outside the modelled envelope";
          return false;
        }
        break;
      case FaultType::kNoiseBurst:
        if (s.scalar <= 0.0) {
          error = where + "noise burst sigma must be strictly positive";
          return false;
        }
        break;
      case FaultType::kBiasRamp:
      case FaultType::kGnssPositionRamp:
        if (s.duration_s <= 0.0) {
          error = where + "a ramp needs a finite positive duration";
          return false;
        }
        if (s.amplitude.squaredNorm() == 0.0) {
          error = where + "a ramp with zero amplitude has no effect";
          return false;
        }
        break;
      case FaultType::kGnssPositionStep:
      case FaultType::kGnssVelocityInconsistency:
        if (s.target != SensorId::kGnss) {
          error = where + "gnss fault targets the gnss sensor only";
          return false;
        }
        break;
      case FaultType::kImuAccelBias:
      case FaultType::kImuGyroBias:
        if (s.target != SensorId::kImu) {
          error = where + "imu fault targets the imu sensor only";
          return false;
        }
        break;
      case FaultType::kPseudorangeOutlier:
        if (s.satellite_index < 0) {
          error = where + "satellite_index must be >= 0";
          return false;
        }
        break;
      case FaultType::kSourceUnavailable:
      case FaultType::kFreeze:
      case FaultType::kBiasStep: break;
    }
  }
  specs_ = specs;
  states_.assign(specs_.size(), State{});
  return true;
}

void FaultInjectionEngine::reset(std::uint64_t run_seed) {
  rng_ = makeStream(run_seed, StreamId::kFaultEngine);
  states_.assign(specs_.size(), State{});
}

bool FaultInjectionEngine::matches(const FaultSpec& spec, const Measurement& m) const {
  if (m.header.sensor != spec.target) return false;
  if (spec.use_type_filter && m.type != spec.type_filter) return false;
  switch (spec.type) {
    case FaultType::kGnssPositionStep:
    case FaultType::kGnssPositionRamp: return m.type == MeasurementType::kGnssPosition;
    case FaultType::kGnssVelocityInconsistency: return m.type == MeasurementType::kGnssVelocity;
    case FaultType::kImuAccelBias:
    case FaultType::kImuGyroBias: return m.type == MeasurementType::kImuSample;
    case FaultType::kVisionDegrade: return m.type == MeasurementType::kVisionRelative;
    case FaultType::kPseudorangeOutlier:
      return m.type == MeasurementType::kPseudorange &&
             static_cast<int>(m.header.sequence % 100u) == spec.satellite_index;
    default: return true;
  }
}

double FaultInjectionEngine::rampFraction(const FaultSpec& spec, const State& state,
                                          double now_s) const {
  if (spec.duration_s <= 0.0) return 1.0;
  const double t0 = state.armed_at_s;
  if (t0 < 0.0) return 0.0;
  double f = (now_s - t0) / spec.duration_s;
  if (f < 0.0) f = 0.0;
  if (f > 1.0) f = 1.0;
  return f;
}

void FaultInjectionEngine::applyOne(const FaultSpec& spec, State& state, Measurement& m,
                                    double now_s) {
  switch (spec.type) {
    case FaultType::kSourceUnavailable:
      m.header.validity = Validity::kUnavailable;
      m.quality = 0.0;
      break;

    case FaultType::kFreeze: {
      // FI-002: replay the last pre-fault sample. scalar >= 1 also freezes the
      // timestamp, which is what makes SCN-008 detectable by age alone.
      if (!state.has_frozen) {
        state.frozen = m;
        state.has_frozen = true;
      }
      const std::uint64_t original_sequence = m.header.sequence;
      const double original_delivery = m.header.delivery_time_s;
      m.v = state.frozen.v;
      m.quality = state.frozen.quality;
      if (spec.scalar >= 1.0) {
        m.header.sample_time_s = state.frozen.header.sample_time_s;
        m.header.sequence = state.frozen.header.sequence;  // INT-019: repeated sequence
      } else {
        m.header.sequence = original_sequence;
      }
      m.header.delivery_time_s = original_delivery;
      break;
    }

    case FaultType::kBiasStep:
    case FaultType::kGnssPositionStep:
    case FaultType::kGnssVelocityInconsistency: {
      const int n = m.dim < 3 ? m.dim : 3;
      for (int i = 0; i < n; ++i) m.v[static_cast<std::size_t>(i)] += spec.amplitude[i];
      break;
    }

    case FaultType::kBiasRamp:
    case FaultType::kGnssPositionRamp: {
      const double f = rampFraction(spec, state, now_s);
      const int n = m.dim < 3 ? m.dim : 3;
      for (int i = 0; i < n; ++i) m.v[static_cast<std::size_t>(i)] += spec.amplitude[i] * f;
      break;
    }

    case FaultType::kImuAccelBias:
      for (int i = 0; i < 3; ++i) m.v[static_cast<std::size_t>(i)] += spec.amplitude[i];
      break;

    case FaultType::kImuGyroBias:
      for (int i = 0; i < 3; ++i) m.v[static_cast<std::size_t>(i + 3)] += spec.amplitude[i];
      break;

    case FaultType::kNoiseBurst:
      for (int i = 0; i < m.dim; ++i) {
        m.v[static_cast<std::size_t>(i)] += spec.scalar * rng_.nextGaussian();
      }
      break;

    case FaultType::kLatency:
      // SENS-013: sample_time is deliberately left untouched, only delivery
      // slips. That is what lets the integrity monitor see the true age.
      m.header.delivery_time_s += spec.scalar;
      break;

    case FaultType::kDropProbabilistic: {
      double p = spec.scalar;
      if (spec.scalar_final >= 0.0) {
        p = spec.scalar + (spec.scalar_final - spec.scalar) * rampFraction(spec, state, now_s);
      }
      if (rng_.nextBernoulli(p)) {
        m.header.validity = Validity::kDropped;
      }
      break;
    }

    case FaultType::kVisionDegrade: {
      double q = spec.scalar;
      if (spec.scalar_final >= 0.0) {
        q = spec.scalar + (spec.scalar_final - spec.scalar) * rampFraction(spec, state, now_s);
      }
      // SENS-018 / SCN-010: quality falls and noise grows, but the source keeps
      // publishing. It must not silently vanish.
      if (q < m.quality) {
        const double inflation = m.quality / (q > 0.02 ? q : 0.02);
        for (int i = 0; i < m.dim; ++i) {
          m.v[static_cast<std::size_t>(i)] += (inflation - 1.0) * 0.5 * rng_.nextGaussian();
        }
        m.quality = q;
      }
      break;
    }

    case FaultType::kPseudorangeOutlier: m.v[0] += spec.scalar; break;
  }
}

void FaultInjectionEngine::apply(std::vector<Measurement>& measurements, double now_s,
                                 MissionPhase phase, std::vector<FaultEvent>& events_out) {
  for (std::size_t i = 0; i < specs_.size(); ++i) {
    const FaultSpec& spec = specs_[i];
    State& st = states_[i];

    // --- arming -------------------------------------------------------------
    if (!st.armed) {
      const bool arm =
          spec.use_phase_trigger ? (phase == spec.trigger_phase) : (now_s >= spec.start_s);
      if (arm) {
        st.armed = true;
        st.armed_at_s = now_s;
      }
    }

    const bool within = st.armed && now_s >= st.armed_at_s &&
                        (spec.duration_s < 0.0 || now_s <= st.armed_at_s + spec.duration_s);

    if (within && !st.active) {
      st.active = true;
      events_out.push_back({now_s, spec.id, spec.type, spec.target, true});
    } else if (!within && st.active) {
      st.active = false;
      st.has_frozen = false;  // a re-armed freeze latches a fresh sample
      events_out.push_back({now_s, spec.id, spec.type, spec.target, false});
    }
    if (!st.active) continue;

    for (Measurement& m : measurements) {
      if (!matches(spec, m)) continue;
      // An already unavailable or dropped record is not resurrected by a later
      // fault in the chain; the order of application is the scenario order.
      if (m.header.validity == Validity::kUnavailable || m.header.validity == Validity::kDropped) {
        continue;
      }
      applyOne(spec, st, m, now_s);
      m.validateNumeric();  // SENS-014: a fault must not smuggle a NaN through
    }
  }
}

}  // namespace aerolab
