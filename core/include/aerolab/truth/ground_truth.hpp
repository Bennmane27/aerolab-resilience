// AEROLAB RESILIENCE - ground truth simulator (subsystem S1).
//
// Requirements: SIM-001..SIM-014, SYS-002, SYS-015.
//
// Model
//   The V1 truth is a deterministic parametric trajectory, not an aerodynamic
//   model (decision D-006). Horizontal ground speed follows an analytic speed
//   schedule; the horizontal track is a chain of constant curvature segments;
//   the vertical profile is a glideslope followed by a Hermite flare. Every
//   quantity the sensors need - position, velocity, acceleration, attitude,
//   angular rate, specific force - is available in closed form at any time,
//   so the truth carries no integration state and cannot drift.
//
//   The only non closed-form quantities are the Euler rates, obtained by a
//   five point central difference of the analytic attitude angles with
//   h = 1e-3 s. The resulting error is ~1e-11 rad/s, nine orders of magnitude
//   below the gyro noise floor. Documented in docs/methodology/truth.md.
//
// Simplifications (NFR-024)
//   * Zero angle of attack: pitch equals the flight path angle. A real aircraft
//     holds ~3 deg of alpha on approach.
//   * No wind, therefore no crab: yaw equals track heading.
//   * Coordinated turns: bank = atan(V * psi_dot / g), no sideslip.
//   * Flat earth, local NED, no Coriolis or transport rate. Valid for a
//     10 minute, 10 km footprint at the accuracy levels benchmarked here.
#pragma once

#include <string>

#include "aerolab/core/types.hpp"
#include "aerolab/truth/path.hpp"
#include "aerolab/truth/truth_state.hpp"

namespace aerolab {

// SIM-008: bounds are configuration, not hard coded, and are asserted by tests.
struct TrajectoryProfile {
  std::string name{"approach_straight"};

  double approach_speed_mps{70.0};
  double taxi_speed_mps{8.0};
  double glideslope_rad{3.0 * kDegToRad};
  double aim_point_beyond_threshold_m{300.0};
  double flare_height_m{12.0};
  // Flare duration factor. T_flare = k * h_flare / (V * tan(gamma)).
  // k = 1.75 makes the Hermite height polynomial monotonically decreasing with
  // its derivative vanishing exactly at touchdown, for any V and gamma.
  double flare_time_factor{1.75};
  double rollout_decel_time_s{18.0};

  double initial_distance_to_threshold_m{4000.0};
  // Turn profile only: length of the base leg and the turn radius.
  double base_leg_length_m{1200.0};
  double turn_radius_m{1400.0};
  double turn_sweep_rad{90.0 * kDegToRad};
  bool turn_right{true};
  // Time taken to roll into and out of a turn. Used to blend the curvature the
  // bank angle is derived from, so the attitude stays differentiable at the
  // segment boundaries (see HorizontalPath::curvatureSmoothed).
  double roll_in_time_s{3.0};

  double max_speed_mps{120.0};        // SIM-008 validation bound
  double max_acceleration_mps2{6.0};  // SIM-008 validation bound
};

bool parseTrajectoryProfileName(const std::string& name, TrajectoryProfile& out);

class GroundTruthSimulator {
 public:
  GroundTruthSimulator() { reset(TrajectoryProfile{}, RunwayScene{}); }

  // SYS-003: a full reset without recreating the object.
  void reset(const TrajectoryProfile& profile, const RunwayScene& scene);

  void step(double dt_s) {
    t_s_ += dt_s;
    current_ = sampleAt(t_s_);
  }

  const TruthState& state() const { return current_; }
  double time_s() const { return t_s_; }

  // Analytic evaluation at an arbitrary time. Pure function of the profile.
  TruthState sampleAt(double t_s) const;

  const TrajectoryProfile& profile() const { return profile_; }
  const RunwayScene& scene() const { return scene_; }

  // Exposed for tests and for the vision sensor geometry.
  double flareStartTime_s() const { return t_flare_start_s_; }
  double touchdownTime_s() const { return t_touchdown_s_; }
  double distanceToThresholdAt_m(double t_s) const;

 private:
  struct Kinematics {
    double s_m{0.0};         // horizontal arc length travelled
    double speed_mps{0.0};   // horizontal ground speed
    double accel_mps2{0.0};  // horizontal along-track acceleration
  };

  Kinematics longitudinalAt(double t_s) const;
  void verticalAt(double t_s, const Kinematics& k, double& height_m, double& height_rate_mps,
                  double& height_accel_mps2) const;
  void attitudeAt(double t_s, double& roll_rad, double& pitch_rad, double& yaw_rad) const;
  MissionPhase phaseAt(double t_s, const Kinematics& k, double height_m) const;

  TrajectoryProfile profile_{};
  RunwayScene scene_{};
  HorizontalPath path_{};
  double s_threshold_m_{0.0};

  double t_flare_start_s_{0.0};
  double t_flare_duration_s_{0.0};
  double t_touchdown_s_{0.0};
  double flare_start_height_m_{0.0};
  double flare_start_rate_mps_{0.0};

  double t_s_{0.0};
  TruthState current_{};
};

}  // namespace aerolab
