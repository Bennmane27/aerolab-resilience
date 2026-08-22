#include "aerolab/navigation/baseline_estimators.hpp"

#include <cmath>

namespace aerolab {

// ---------------------------------------------------------------- NAV-A ------

void GnssOnlyEstimator::initialize(const EstimatorConfig& config, const NavSolution& seed) {
  config_ = config;
  seed_ = seed;
  reset();
}

void GnssOnlyEstimator::reset() {
  t_s_ = seed_.t_s;
  has_fix_ = false;
  last_fix_s_ = -1.0e9;
  position_ned_m_ = seed_.position_ned_m;
  velocity_ned_mps_ = seed_.velocity_ned_mps;
}

bool GnssOnlyEstimator::prepareUpdate(const Measurement& m, InnovationInfo& info) {
  info = InnovationInfo{};
  if (!m.usable()) return false;
  if (m.type != MeasurementType::kGnssPosition && m.type != MeasurementType::kGnssVelocity) {
    return false;
  }
  info.measurement_age_s = t_s_ - m.header.sample_time_s;
  info.dim = 3;
  // NAV-A has no state model, so the innovation is the difference against the
  // previous fix. It carries no integrity information and the NIS is reported
  // only so the telemetry schema stays uniform across estimators.
  const Vec3 reference =
      m.type == MeasurementType::kGnssPosition ? position_ned_m_ : velocity_ned_mps_;
  info.y = ColVec3(m.v[0] - reference.x, m.v[1] - reference.y, m.v[2] - reference.z);
  const double sigma = m.type == MeasurementType::kGnssPosition ? config_.gnss_sigma_horizontal_m
                                                                : config_.gnss_sigma_velocity_mps;
  info.R = Mat(3, 3);
  info.S = Mat(3, 3);
  for (int i = 0; i < 3; ++i) {
    info.R(i, i) = sigma * sigma;
    info.S(i, i) = 2.0 * sigma * sigma;
  }
  info.H = Mat(3, 3);
  double nis = 0.0;
  if (quadraticFormInv(info.S, info.y, nis)) info.nis = nis;
  info.valid = has_fix_;  // the very first fix has nothing to compare against
  if (!has_fix_) info.valid = true;
  return true;
}

void GnssOnlyEstimator::applyUpdate(const Measurement& m, const InnovationInfo&) {
  if (m.type == MeasurementType::kGnssPosition) {
    position_ned_m_ = Vec3(m.v[0], m.v[1], m.v[2]);
    has_fix_ = true;
    last_fix_s_ = m.header.sample_time_s;
  } else if (m.type == MeasurementType::kGnssVelocity) {
    velocity_ned_mps_ = Vec3(m.v[0], m.v[1], m.v[2]);
  }
}

NavSolution GnssOnlyEstimator::solution() const {
  NavSolution s;
  s.t_s = t_s_;
  s.position_ned_m = position_ned_m_;
  s.velocity_ned_mps = velocity_ned_mps_;
  s.attitude_body_to_ned = Quat::Identity();
  const double age = t_s_ - last_fix_s_;
  s.last_absolute_fix_age_s = has_fix_ ? age : t_s_;
  s.position_covariance_m2 = Mat(3, 3);
  const double sh = config_.gnss_sigma_horizontal_m;
  const double sv = config_.gnss_sigma_vertical_m;
  s.position_covariance_m2(0, 0) = sh * sh;
  s.position_covariance_m2(1, 1) = sh * sh;
  s.position_covariance_m2(2, 2) = sv * sv;

  if (!has_fix_) {
    s.mode = NavMode::kInitializing;
    s.valid = false;
  } else if (age > config_.gnss_only_timeout_s) {
    // INT-014: when the only source is gone the solution is declared unusable
    // instead of freezing on a stale fix and pretending it is current.
    s.mode = NavMode::kUnsafe;
    s.valid = false;
  } else {
    s.mode = NavMode::kNormal;
    s.valid = true;
  }
  return s;
}

// ---------------------------------------------------------------- NAV-B ------

void InsDeadReckoningEstimator::initialize(const EstimatorConfig& config, const NavSolution& seed) {
  config_ = config;
  seed_ = seed;
  reset();
}

void InsDeadReckoningEstimator::reset() {
  t_s_ = seed_.t_s;
  position_ned_m_ = seed_.position_ned_m;
  velocity_ned_mps_ = seed_.velocity_ned_mps;
  attitude_ = seed_.attitude_body_to_ned.normalized();
  accel_bias_mps2_ = seed_.accel_bias_mps2;
  gyro_bias_radps_ = seed_.gyro_bias_radps;
  last_specific_force_body_ = Vec3(0.0, 0.0, -kGravityMps2);
  last_angular_rate_body_ = Vec3::Zero();
  drift_time_s_ = 0.0;
  healthy_ = true;
  diagnostic_.clear();
}

void InsDeadReckoningEstimator::integrate(const Vec3& specific_force_body,
                                          const Vec3& angular_rate_body, double dt_s) {
  if (dt_s <= 0.0) return;
  const Vec3 a_b = specific_force_body - accel_bias_mps2_;
  const Vec3 w_b = angular_rate_body - gyro_bias_radps_;
  const Vec3 a_n = attitude_.rotate(a_b) + Vec3(0.0, 0.0, kGravityMps2);
  position_ned_m_ += velocity_ned_mps_ * dt_s + a_n * (0.5 * dt_s * dt_s);
  velocity_ned_mps_ += a_n * dt_s;
  attitude_ = (attitude_ * Quat::FromRotationVector(w_b * dt_s)).normalized();
  t_s_ += dt_s;
  drift_time_s_ += dt_s;
  last_specific_force_body_ = specific_force_body;
  last_angular_rate_body_ = angular_rate_body;
  if (!position_ned_m_.isFinite() || !velocity_ned_mps_.isFinite() || !attitude_.isFinite()) {
    healthy_ = false;
    if (diagnostic_.empty()) diagnostic_ = "non-finite inertial state";
  }
}

void InsDeadReckoningEstimator::predict(double dt_s) {
  integrate(last_specific_force_body_, last_angular_rate_body_, dt_s);
}

void InsDeadReckoningEstimator::consumeImu(const Measurement& imu) {
  if (imu.type != MeasurementType::kImuSample || !imu.usable()) return;
  const double dt = imu.header.sample_time_s - t_s_;
  const Vec3 f(imu.v[0], imu.v[1], imu.v[2]);
  const Vec3 w(imu.v[3], imu.v[4], imu.v[5]);
  if (dt > 0.0) {
    integrate(f, w, dt);
  } else {
    last_specific_force_body_ = f;
    last_angular_rate_body_ = w;
  }
}

NavSolution InsDeadReckoningEstimator::solution() const {
  NavSolution s;
  s.t_s = t_s_;
  s.position_ned_m = position_ned_m_;
  s.velocity_ned_mps = velocity_ned_mps_;
  s.attitude_body_to_ned = attitude_;
  s.accel_bias_mps2 = accel_bias_mps2_;
  s.gyro_bias_radps = gyro_bias_radps_;
  s.mode = healthy_ ? NavMode::kDeadReckoning : NavMode::kUnsafe;
  s.valid = healthy_;
  s.last_absolute_fix_age_s = drift_time_s_;

  // Analytic growth of the reported uncertainty: alignment velocity error
  // integrates linearly, residual accelerometer bias quadratically.
  const double t = drift_time_s_;
  const double sigma_p = config_.init_sigma_position_m + config_.init_sigma_velocity_mps * t +
                         0.5 * config_.init_sigma_accel_bias_mps2 * t * t;
  s.position_covariance_m2 = Mat(3, 3);
  s.position_covariance_m2(0, 0) = sigma_p * sigma_p;
  s.position_covariance_m2(1, 1) = sigma_p * sigma_p;
  s.position_covariance_m2(2, 2) = sigma_p * sigma_p;
  return s;
}

}  // namespace aerolab
