#include "aerolab/truth/ground_truth.hpp"

#include <cmath>

namespace aerolab {
namespace {

// Five point central difference step for the Euler rates. 1e-3 s balances
// truncation error (O(h^4) ~ 1e-12) against cancellation (~1e-11 rad/s).
constexpr double kAngleDiffStep_s = 1.0e-3;

// Smoothstep 3u^2 - 2u^3 and its integral / derivative, used for the rollout
// deceleration so that speed is C1 and acceleration is continuous.
inline double smoothStep(double u) {
  return u * u * (3.0 - 2.0 * u);
}
inline double smoothStepIntegral(double u) {
  return u * u * u * (1.0 - 0.5 * u);
}
inline double smoothStepDerivative(double u) {
  return 6.0 * u * (1.0 - u);
}

}  // namespace

bool parseTrajectoryProfileName(const std::string& name, TrajectoryProfile& out) {
  TrajectoryProfile p;
  p.name = name;
  if (name == "approach_straight" || name == "approach_ils_like") {
    p.initial_distance_to_threshold_m = 4000.0;
    out = p;
    return true;
  }
  if (name == "approach_turn") {
    p.initial_distance_to_threshold_m = 6000.0;
    out = p;
    return true;
  }
  if (name == "taxi") {
    p.initial_distance_to_threshold_m = 0.0;
    out = p;
    return true;
  }
  return false;
}

void GroundTruthSimulator::reset(const TrajectoryProfile& profile, const RunwayScene& scene) {
  profile_ = profile;
  scene_ = scene;
  t_s_ = 0.0;
  path_.clear();

  const bool ground_only = (profile_.name == "taxi");
  const double psi_r = scene_.heading_rad;
  // 10 minutes of taxi at taxi speed plus margin (SIM-006).
  const double tail_m = profile_.taxi_speed_mps * 700.0 + 5000.0;

  if (ground_only) {
    path_.setStart(0.0, 0.0, psi_r);
    path_.addStraight(400.0);
    path_.addArc(profile_.turn_right ? 60.0 : -60.0, profile_.turn_right ? kPi * 0.5 : -kPi * 0.5);
    path_.addStraight(tail_m);
    s_threshold_m_ = 0.0;
    path_.translateSoThat(0.0, scene_.threshold_ned_m.x, scene_.threshold_ned_m.y);
  } else if (profile_.name == "approach_turn") {
    const double sweep = profile_.turn_right ? profile_.turn_sweep_rad : -profile_.turn_sweep_rad;
    const double radius = profile_.turn_right ? profile_.turn_radius_m : -profile_.turn_radius_m;
    const double arc_len = std::fabs(sweep) * profile_.turn_radius_m;
    double final_len =
        profile_.initial_distance_to_threshold_m - profile_.base_leg_length_m - arc_len;
    if (final_len < 800.0) final_len = 800.0;
    path_.setStart(0.0, 0.0, wrapPi(psi_r - sweep));
    path_.addStraight(profile_.base_leg_length_m);
    path_.addArc(radius, sweep);
    path_.addStraight(final_len + tail_m);
    s_threshold_m_ = profile_.base_leg_length_m + arc_len + final_len;
    path_.translateSoThat(s_threshold_m_, scene_.threshold_ned_m.x, scene_.threshold_ned_m.y);
  } else {  // approach_straight / approach_ils_like
    path_.setStart(0.0, 0.0, psi_r);
    path_.addStraight(profile_.initial_distance_to_threshold_m + tail_m);
    s_threshold_m_ = profile_.initial_distance_to_threshold_m;
    path_.translateSoThat(s_threshold_m_, scene_.threshold_ned_m.x, scene_.threshold_ned_m.y);
  }

  if (ground_only) {
    t_flare_start_s_ = 0.0;
    t_flare_duration_s_ = 0.0;
    t_touchdown_s_ = 0.0;
    flare_start_height_m_ = 0.0;
    flare_start_rate_mps_ = 0.0;
  } else {
    const double tan_gamma = std::tan(profile_.glideslope_rad);
    const double v = profile_.approach_speed_mps;
    // Arc length at which the glideslope height equals the flare entry height.
    const double s_flare = s_threshold_m_ + profile_.aim_point_beyond_threshold_m -
                           profile_.flare_height_m / tan_gamma;
    t_flare_start_s_ = s_flare / v;  // speed is constant up to touchdown
    flare_start_height_m_ = profile_.flare_height_m;
    flare_start_rate_mps_ = -v * tan_gamma;
    t_flare_duration_s_ = profile_.flare_time_factor * profile_.flare_height_m / (v * tan_gamma);
    t_touchdown_s_ = t_flare_start_s_ + t_flare_duration_s_;
  }

  current_ = sampleAt(0.0);
}

GroundTruthSimulator::Kinematics GroundTruthSimulator::longitudinalAt(double t_s) const {
  Kinematics k;
  const bool ground_only = (profile_.name == "taxi");
  const double v_app = ground_only ? profile_.taxi_speed_mps : profile_.approach_speed_mps;
  const double v_taxi = profile_.taxi_speed_mps;
  const double t_td = t_touchdown_s_;
  const double t_dec = profile_.rollout_decel_time_s;

  if (t_s <= 0.0) {
    k.s_m = v_app * t_s;  // negative time extends backwards, used by the FD stencil
    k.speed_mps = v_app;
    k.accel_mps2 = 0.0;
    return k;
  }
  if (ground_only || t_s <= t_td) {
    k.s_m = v_app * t_s;
    k.speed_mps = v_app;
    k.accel_mps2 = 0.0;
    return k;
  }
  const double s_td = v_app * t_td;
  const double dv = v_taxi - v_app;
  if (t_s <= t_td + t_dec) {
    const double u = (t_s - t_td) / t_dec;
    k.s_m = s_td + t_dec * (v_app * u + dv * smoothStepIntegral(u));
    k.speed_mps = v_app + dv * smoothStep(u);
    k.accel_mps2 = dv * smoothStepDerivative(u) / t_dec;
    return k;
  }
  const double s_end = s_td + t_dec * (v_app + 0.5 * dv);
  k.s_m = s_end + v_taxi * (t_s - t_td - t_dec);
  k.speed_mps = v_taxi;
  k.accel_mps2 = 0.0;
  return k;
}

void GroundTruthSimulator::verticalAt(double t_s, const Kinematics& k, double& height_m,
                                      double& height_rate_mps, double& height_accel_mps2) const {
  if (profile_.name == "taxi") {
    height_m = 0.0;
    height_rate_mps = 0.0;
    height_accel_mps2 = 0.0;
    return;
  }
  if (t_s >= t_touchdown_s_) {
    height_m = 0.0;
    height_rate_mps = 0.0;
    height_accel_mps2 = 0.0;
    return;
  }
  if (t_s <= t_flare_start_s_) {
    // Glideslope aimed at a point beyond the threshold: h = (d + aim) tan(gamma)
    const double tan_gamma = std::tan(profile_.glideslope_rad);
    const double d_m = s_threshold_m_ - k.s_m;
    height_m = (d_m + profile_.aim_point_beyond_threshold_m) * tan_gamma;
    height_rate_mps = -k.speed_mps * tan_gamma;
    height_accel_mps2 = -k.accel_mps2 * tan_gamma;
    return;
  }
  // Cubic Hermite flare: h(0) = h_f, h'(0) = -V tan(gamma), h(T) = 0, h'(T) = 0.
  // With T = 1.75 * h_f / (V tan gamma) the derivative has a single root at
  // u = 1, so the height is monotonically decreasing over the whole flare.
  const double tflare = t_flare_duration_s_;
  const double u = (t_s - t_flare_start_s_) / tflare;
  const double h0 = flare_start_height_m_;
  const double m0 = flare_start_rate_mps_ * tflare;  // scaled tangent
  const double h00 = 2.0 * u * u * u - 3.0 * u * u + 1.0;
  const double h10 = u * u * u - 2.0 * u * u + u;
  height_m = h0 * h00 + m0 * h10;
  const double dh00 = 6.0 * u * u - 6.0 * u;
  const double dh10 = 3.0 * u * u - 4.0 * u + 1.0;
  height_rate_mps = (h0 * dh00 + m0 * dh10) / tflare;
  const double ddh00 = 12.0 * u - 6.0;
  const double ddh10 = 6.0 * u - 4.0;
  height_accel_mps2 = (h0 * ddh00 + m0 * ddh10) / (tflare * tflare);
}

void GroundTruthSimulator::attitudeAt(double t_s, double& roll_rad, double& pitch_rad,
                                      double& yaw_rad) const {
  const Kinematics k = longitudinalAt(t_s);
  const PathPoint p = path_.evaluate(k.s_m);
  double h_m = 0.0;
  double hdot = 0.0;
  double hddot = 0.0;
  verticalAt(t_s, k, h_m, hdot, hddot);

  yaw_rad = p.heading_rad;
  pitch_rad = std::atan2(hdot, k.speed_mps > 1e-6 ? k.speed_mps : 1e-6);
  // Bank is derived from the SMOOTHED curvature so the attitude stays
  // differentiable across segment boundaries; yaw still follows the exact path.
  const double blend_m = profile_.roll_in_time_s * profile_.approach_speed_mps;
  const double curvature_for_bank = path_.curvatureSmoothed(k.s_m, blend_m);
  const double bank_yaw_rate = curvature_for_bank * k.speed_mps;
  roll_rad = std::atan(k.speed_mps * bank_yaw_rate / kGravityMps2);
}

MissionPhase GroundTruthSimulator::phaseAt(double t_s, const Kinematics& k, double height_m) const {
  if (profile_.name == "taxi") return MissionPhase::kTaxi;
  if (t_s >= t_touchdown_s_) {
    return k.speed_mps > 15.0 ? MissionPhase::kRollout : MissionPhase::kTaxi;
  }
  if (t_s >= t_flare_start_s_) return MissionPhase::kFlare;
  const PathPoint p = path_.evaluate(k.s_m);
  if (std::fabs(p.curvature_per_m) > 1.0e-9) return MissionPhase::kTurn;
  const double d_m = s_threshold_m_ - k.s_m;
  if (d_m < 2000.0) return MissionPhase::kFinalApproach;
  if (height_m > 0.0) return MissionPhase::kDescent;
  return MissionPhase::kCruise;
}

double GroundTruthSimulator::distanceToThresholdAt_m(double t_s) const {
  return s_threshold_m_ - longitudinalAt(t_s).s_m;
}

TruthState GroundTruthSimulator::sampleAt(double t_s) const {
  const Kinematics k = longitudinalAt(t_s);
  const PathPoint p = path_.evaluate(k.s_m);
  double h_m = 0.0;
  double hdot_mps = 0.0;
  double hddot_mps2 = 0.0;
  verticalAt(t_s, k, h_m, hdot_mps, hddot_mps2);

  TruthState st;
  st.t_s = t_s;
  st.position_ned_m = Vec3(p.north_m, p.east_m, -h_m);

  const double cpsi = std::cos(p.heading_rad);
  const double spsi = std::sin(p.heading_rad);
  st.velocity_ned_mps = Vec3(k.speed_mps * cpsi, k.speed_mps * spsi, -hdot_mps);

  const double yaw_rate = p.curvature_per_m * k.speed_mps;
  const Vec3 accel_ned(k.accel_mps2 * cpsi - k.speed_mps * yaw_rate * spsi,
                       k.accel_mps2 * spsi + k.speed_mps * yaw_rate * cpsi, -hddot_mps2);

  double roll = 0.0;
  double pitch = 0.0;
  double yaw = 0.0;
  attitudeAt(t_s, roll, pitch, yaw);
  st.attitude_body_to_ned = Quat::FromEulerZyx(roll, pitch, yaw);

  // Euler rates by five point central difference of the analytic angles.
  const double h = kAngleDiffStep_s;
  double d_roll[4];
  double d_pitch[4];
  double d_yaw[4];
  const double offsets[4] = {-2.0 * h, -h, h, 2.0 * h};
  for (int i = 0; i < 4; ++i) {
    double r2 = 0.0;
    double p2 = 0.0;
    double y2 = 0.0;
    attitudeAt(t_s + offsets[i], r2, p2, y2);
    d_roll[i] = wrapPi(r2 - roll);
    d_pitch[i] = wrapPi(p2 - pitch);
    d_yaw[i] = wrapPi(y2 - yaw);
  }
  const double inv = 1.0 / (12.0 * h);
  const double roll_rate = (d_roll[0] - 8.0 * d_roll[1] + 8.0 * d_roll[2] - d_roll[3]) * inv;
  const double pitch_rate = (d_pitch[0] - 8.0 * d_pitch[1] + 8.0 * d_pitch[2] - d_pitch[3]) * inv;
  const double yaw_rate_fd = (d_yaw[0] - 8.0 * d_yaw[1] + 8.0 * d_yaw[2] - d_yaw[3]) * inv;

  // Body rates from Euler rates (ZYX convention, see quaternion.hpp).
  const double sr = std::sin(roll), cr = std::cos(roll);
  const double sp = std::sin(pitch), cp = std::cos(pitch);
  st.angular_rate_body_radps =
      Vec3(roll_rate - yaw_rate_fd * sp, pitch_rate * cr + yaw_rate_fd * cp * sr,
           -pitch_rate * sr + yaw_rate_fd * cp * cr);

  // Specific force: f = a - g, expressed in body axes. In NED gravity points
  // down (+z), so a stationary accelerometer reads (0, 0, -9.80665) in body.
  const Vec3 gravity_ned(0.0, 0.0, kGravityMps2);
  const Vec3 specific_force_ned = accel_ned - gravity_ned;
  st.specific_force_body_mps2 = st.attitude_body_to_ned.rotateInverse(specific_force_ned);

  st.phase = phaseAt(t_s, k, h_m);
  return st;
}

}  // namespace aerolab
