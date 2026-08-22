// AEROLAB RESILIENCE - fault injection engine (subsystem S3).
//
// Requirements: FI-001..FI-020, SYS-011, AT-002.
//
// SAFETY BOUNDARY (section 3.2 of the cahier des charges, SYS-011)
//   Everything in this file is arithmetic on an array of doubles that a
//   simulated sensor produced a few microseconds earlier. There is no radio,
//   no signal generator, no receiver parameter, no transmission procedure and
//   no model of one. "Spoofing" here means: add a number to a synthetic
//   position that never left this process. See SECURITY_AND_SAFETY.md.
//
// STRUCTURAL GUARANTEE (FI-017, AT-002)
//   The engine is not given a handle to the ground truth. Its only input is a
//   vector of measurements. This is enforced by the type of apply(), not by
//   convention, so no future change can quietly make the truth reachable.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "aerolab/core/rng.hpp"
#include "aerolab/math/vec3.hpp"
#include "aerolab/sensors/measurement.hpp"

namespace aerolab {

enum class FaultType : int {
  kSourceUnavailable = 0,          // FI-001
  kFreeze = 1,                     // FI-002
  kBiasStep = 2,                   // FI-003
  kBiasRamp = 3,                   // FI-004
  kNoiseBurst = 4,                 // FI-005
  kLatency = 5,                    // FI-006
  kDropProbabilistic = 6,          // FI-007
  kGnssPositionStep = 7,           // FI-008
  kGnssPositionRamp = 8,           // FI-009
  kGnssVelocityInconsistency = 9,  // FI-010
  kImuAccelBias = 10,              // FI-011
  kImuGyroBias = 11,               // FI-012
  kVisionDegrade = 12,             // FI-013
  kPseudorangeOutlier = 13,        // SCN-013 / SENS-020
};

const char* toString(FaultType t);
bool parseFaultType(const std::string& name, FaultType& out);

struct FaultSpec {
  std::string id{"F-001"};
  FaultType type{FaultType::kGnssPositionStep};
  SensorId target{SensorId::kGnss};

  double start_s{0.0};
  double duration_s{-1.0};  // < 0 means "until the end of the run"

  // FI-020 (SHOULD): arm on a mission phase instead of an absolute instant.
  bool use_phase_trigger{false};
  MissionPhase trigger_phase{MissionPhase::kFinalApproach};

  // Primary vector amplitude. Meaning by type:
  //   kBiasStep / kBiasRamp / kGnssPositionStep / kGnssPositionRamp -> N,E,D [m]
  //   kGnssVelocityInconsistency                                   -> vN,vE,vD [m/s]
  //   kImuAccelBias                                                -> body x,y,z [m/s^2]
  //   kImuGyroBias                                                 -> body x,y,z [rad/s]
  Vec3 amplitude{};

  // Scalar amplitude. Meaning by type:
  //   kNoiseBurst        -> extra sigma, in the units of the target payload
  //   kLatency           -> added delivery delay [s]
  //   kDropProbabilistic -> drop probability at fault start [0..1]
  //   kFreeze            -> 0 = freeze value only, 1 = freeze value and timestamp
  //   kVisionDegrade     -> quality at fault start [0..1]
  //   kPseudorangeOutlier-> range bias [m]
  double scalar{0.0};
  // Terminal value for the ramped variants of the scalar parameters
  // (kDropProbabilistic, kVisionDegrade). Ignored when negative.
  double scalar_final{-1.0};

  // Restrict the fault to one measurement type of the target sensor. Without
  // it, a GNSS fault would hit position and velocity alike.
  bool use_type_filter{false};
  MeasurementType type_filter{MeasurementType::kGnssPosition};

  // Satellite index for kPseudorangeOutlier (SENS-020).
  int satellite_index{0};

  double end_s() const { return duration_s < 0.0 ? 1.0e300 : start_s + duration_s; }
};

// FI-015: start and end of every fault are recorded so that Time to Detect and
// Time to Isolate have an unambiguous t0 (M-05, M-06).
struct FaultEvent {
  double t_s{0.0};
  std::string fault_id;
  FaultType type{FaultType::kSourceUnavailable};
  SensorId target{SensorId::kGnss};
  bool activated{true};  // true = start, false = end
};

class FaultInjectionEngine {
 public:
  // FI-018: rejects incoherent amplitudes, units and windows before the run
  // starts, so a bad scenario fails at configure time and not silently at
  // tick 4000. Returns false and fills `error` (SYS-010, API-005).
  bool configure(const std::vector<FaultSpec>& specs, std::string& error);

  void reset(std::uint64_t run_seed);

  // FI-017: no ground truth parameter, by construction.
  // `phase` is the mission phase reported by the truth simulator and is used
  // only to arm phase triggered faults (FI-020). It carries no state.
  void apply(std::vector<Measurement>& measurements, double now_s, MissionPhase phase,
             std::vector<FaultEvent>& events_out);

  const std::vector<FaultSpec>& specs() const { return specs_; }

  // Activation window actually used at run time, resolved for phase triggers.
  double resolvedStart_s(std::size_t index) const {
    return index < states_.size() ? states_[index].armed_at_s : -1.0;
  }

  bool anyActive() const {
    for (const State& s : states_) {
      if (s.active) return true;
    }
    return false;
  }

 private:
  struct State {
    bool armed{false};
    bool active{false};
    double armed_at_s{-1.0};
    // kFreeze memory: last measurement seen before the freeze started.
    bool has_frozen{false};
    Measurement frozen{};
  };

  bool matches(const FaultSpec& spec, const Measurement& m) const;
  void applyOne(const FaultSpec& spec, State& state, Measurement& m, double now_s);
  double rampFraction(const FaultSpec& spec, const State& state, double now_s) const;

  std::vector<FaultSpec> specs_;
  std::vector<State> states_;
  Pcg32 rng_{};
};

}  // namespace aerolab
