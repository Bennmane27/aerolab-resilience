// AEROLAB RESILIENCE - ground truth state.
//
// SIM-014 / FI-017 / AT-002: this structure is produced by the ground truth
// simulator and is IMMUTABLE for every downstream module. Sensors read it,
// the fault engine never sees it, estimators must not access it in benchmark
// mode (NAV-006). The compile-time enforcement is that sensors take it by
// const reference and no other subsystem is given a handle at all.
#pragma once

#include "aerolab/core/types.hpp"
#include "aerolab/math/quaternion.hpp"
#include "aerolab/math/vec3.hpp"

namespace aerolab {

struct TruthState {
  double t_s{0.0};
  Vec3 position_ned_m{};            // North, East, Down (Down positive) [m]
  Vec3 velocity_ned_mps{};          // [m/s]
  Quat attitude_body_to_ned{};      // body -> NED, see quaternion.hpp conventions
  Vec3 angular_rate_body_radps{};   // p, q, r in body frame [rad/s]
  Vec3 specific_force_body_mps2{};  // f_b = a_ned - g_ned rotated into body [m/s^2]
  MissionPhase phase{MissionPhase::kFinalApproach};

  // SIM-011: altitude above the synthetic airfield, independent of any
  // barometric model. Down is positive in NED, so altitude is -pD.
  double altitude_m() const { return -position_ned_m.z; }

  double groundSpeed_mps() const {
    return std::sqrt(velocity_ned_mps.x * velocity_ned_mps.x +
                     velocity_ned_mps.y * velocity_ned_mps.y);
  }

  bool isFinite() const {
    return position_ned_m.isFinite() && velocity_ned_mps.isFinite() &&
           attitude_body_to_ned.isFinite() && angular_rate_body_radps.isFinite() &&
           specific_force_body_mps2.isFinite();
  }
};

// Static description of the synthetic airfield, loaded from a scene file
// (SIM-009). The runway frame is used by the vision-relative sensor.
struct RunwayScene {
  Vec3 threshold_ned_m{0.0, 0.0, 0.0};  // landing threshold position
  double heading_rad{0.0};              // runway true heading
  double length_m{3000.0};
  double width_m{45.0};
  double glideslope_rad{3.0 * kDegToRad};
  // Latitude/longitude of the NED origin, used only by the UI adapter (SIM-015).
  double origin_latitude_deg{43.6293};  // Toulouse-Blagnac vicinity, synthetic
  double origin_longitude_deg{1.3638};
  double origin_altitude_m{152.0};
};

}  // namespace aerolab
