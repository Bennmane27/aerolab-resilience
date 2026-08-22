#include "aerolab/navigation/error_state_ekf.hpp"

#include <algorithm>
#include <cmath>

namespace aerolab {
namespace {

Mat blockIdentity(int n, double scale) {
  Mat m(n, n);
  for (int i = 0; i < n; ++i) m(i, i) = scale;
  return m;
}

}  // namespace

void ErrorStateEkf::initialize(const EstimatorConfig& config, const NavSolution& seed) {
  config_ = config;
  nominal_.position_ned_m = seed.position_ned_m;
  nominal_.velocity_ned_mps = seed.velocity_ned_mps;
  nominal_.attitude = seed.attitude_body_to_ned.normalized();
  nominal_.accel_bias_mps2 = seed.accel_bias_mps2;
  nominal_.gyro_bias_radps = seed.gyro_bias_radps;
  initial_nominal_ = nominal_;

  P_ = Mat(kStateDim, kStateDim);
  const double sp = config_.init_sigma_position_m;
  const double sv = config_.init_sigma_velocity_mps;
  const double sa = config_.init_sigma_attitude_rad;
  const double sba = config_.init_sigma_accel_bias_mps2;
  const double sbg = config_.init_sigma_gyro_bias_radps;
  for (int i = 0; i < 3; ++i) {
    P_(kIdxPos + i, kIdxPos + i) = sp * sp;
    P_(kIdxVel + i, kIdxVel + i) = sv * sv;
    P_(kIdxAtt + i, kIdxAtt + i) = sa * sa;
    P_(kIdxAccelBias + i, kIdxAccelBias + i) = sba * sba;
    P_(kIdxGyroBias + i, kIdxGyroBias + i) = sbg * sbg;
  }
  P_initial_ = P_;
  t_s_ = seed.t_s;
  last_absolute_fix_s_ = seed.t_s;
  mode_override_ = NavMode::kInitializing;
  last_specific_force_body_ = Vec3(0.0, 0.0, -kGravityMps2);
  last_angular_rate_body_ = Vec3::Zero();
  snapshots_.clear();
  imu_log_.clear();
  update_log_.clear();
  replaying_ = false;
  healthy_ = true;
  diagnostic_.clear();
  pushSnapshot();
}

void ErrorStateEkf::reset() {
  // NAV-015: byte-for-byte return to the post-initialize state.
  nominal_ = initial_nominal_;
  P_ = P_initial_;
  t_s_ = 0.0;
  last_absolute_fix_s_ = 0.0;
  mode_override_ = NavMode::kInitializing;
  last_specific_force_body_ = Vec3(0.0, 0.0, -kGravityMps2);
  last_angular_rate_body_ = Vec3::Zero();
  snapshots_.clear();
  imu_log_.clear();
  update_log_.clear();
  replaying_ = false;
  healthy_ = true;
  diagnostic_.clear();
  pushSnapshot();
}

void ErrorStateEkf::propagateWithImu(const Vec3& specific_force_body, const Vec3& angular_rate_body,
                                     double dt_s) {
  if (dt_s <= 0.0) return;

  const Vec3 a_b = specific_force_body - nominal_.accel_bias_mps2;
  const Vec3 w_b = angular_rate_body - nominal_.gyro_bias_radps;
  const Mat R = nominal_.attitude.toRotationMatrix();

  // --- nominal propagation ---------------------------------------------------
  const Vec3 a_n = nominal_.attitude.rotate(a_b) + Vec3(0.0, 0.0, kGravityMps2);
  nominal_.position_ned_m += nominal_.velocity_ned_mps * dt_s + a_n * (0.5 * dt_s * dt_s);
  nominal_.velocity_ned_mps += a_n * dt_s;
  nominal_.attitude = (nominal_.attitude * Quat::FromRotationVector(w_b * dt_s)).normalized();

  // --- error state transition F = I + A dt ------------------------------------
  Mat F = Mat::Identity(kStateDim);
  for (int i = 0; i < 3; ++i) F(kIdxPos + i, kIdxVel + i) = dt_s;

  const Mat minus_R_skew_a = (R * skew(a_b)) * (-dt_s);
  F.setBlock(kIdxVel, kIdxAtt, minus_R_skew_a);
  F.setBlock(kIdxVel, kIdxAccelBias, R * (-dt_s));

  Mat att_block = Mat::Identity(3) - skew(w_b) * dt_s;
  F.setBlock(kIdxAtt, kIdxAtt, att_block);
  F.setBlock(kIdxAtt, kIdxGyroBias, blockIdentity(3, -dt_s));

  // --- process noise Q --------------------------------------------------------
  Mat Q(kStateDim, kStateDim);
  const double qa =
      config_.accel_noise_density_mps2_sqrthz * config_.accel_noise_density_mps2_sqrthz * dt_s;
  const double qg =
      config_.gyro_noise_density_radps_sqrthz * config_.gyro_noise_density_radps_sqrthz * dt_s;
  const double qba = config_.accel_bias_walk_mps2_sqrts * config_.accel_bias_walk_mps2_sqrts * dt_s;
  const double qbg = config_.gyro_bias_walk_radps_sqrts * config_.gyro_bias_walk_radps_sqrts * dt_s;
  const double qp =
      config_.position_process_noise_m_sqrts * config_.position_process_noise_m_sqrts * dt_s;
  for (int i = 0; i < 3; ++i) {
    Q(kIdxPos + i, kIdxPos + i) = qp + qa * dt_s * dt_s / 3.0;  // position share of accel noise
    Q(kIdxVel + i, kIdxVel + i) = qa;
    Q(kIdxAtt + i, kIdxAtt + i) = qg;
    Q(kIdxAccelBias + i, kIdxAccelBias + i) = qba;
    Q(kIdxGyroBias + i, kIdxGyroBias + i) = qbg;
  }

  P_ = F * P_ * F.transpose() + Q;
  P_.symmetrize();
  t_s_ += dt_s;
  last_specific_force_body_ = specific_force_body;
  last_angular_rate_body_ = angular_rate_body;
}

void ErrorStateEkf::propagateCovarianceOnly(double dt_s) {
  // Used when no IMU sample is available for this interval: hold the last
  // inertial input. Over one tick the difference is negligible; over a long
  // IMU outage the covariance still grows, which is the behaviour we want.
  propagateWithImu(last_specific_force_body_, last_angular_rate_body_, dt_s);
}

void ErrorStateEkf::predict(double dt_s) {
  if (dt_s <= 0.0) return;
  propagateCovarianceOnly(dt_s);
  if (!replaying_) {
    pushSnapshot();
    trimBuffers();
  }
  checkHealth();
}

void ErrorStateEkf::consumeImu(const Measurement& imu) {
  if (imu.type != MeasurementType::kImuSample || !imu.usable()) return;
  const double dt = imu.header.sample_time_s - t_s_;
  const Vec3 f(imu.v[0], imu.v[1], imu.v[2]);
  const Vec3 w(imu.v[3], imu.v[4], imu.v[5]);
  if (dt > 0.0) {
    propagateWithImu(f, w, dt);
  } else {
    last_specific_force_body_ = f;
    last_angular_rate_body_ = w;
  }
  if (!replaying_) {
    imu_log_.push_back(imu);
    pushSnapshot();
    trimBuffers();
  }
  checkHealth();
}

bool ErrorStateEkf::buildMeasurementModel(const Measurement& m, const Nominal& nominal,
                                          const Mat& P, InnovationInfo& info) const {
  const int n = kStateDim;
  switch (m.type) {
    case MeasurementType::kGnssPosition: {
      if (!gnss_enabled_) return false;
      info.dim = 3;
      info.H = Mat(3, n);
      for (int i = 0; i < 3; ++i) info.H(i, kIdxPos + i) = 1.0;
      info.y = ColVec3(m.v[0] - nominal.position_ned_m.x, m.v[1] - nominal.position_ned_m.y,
                       m.v[2] - nominal.position_ned_m.z);
      info.R = Mat(3, 3);
      info.R(0, 0) = config_.gnss_sigma_horizontal_m * config_.gnss_sigma_horizontal_m;
      info.R(1, 1) = config_.gnss_sigma_horizontal_m * config_.gnss_sigma_horizontal_m;
      info.R(2, 2) = config_.gnss_sigma_vertical_m * config_.gnss_sigma_vertical_m;
      break;
    }
    case MeasurementType::kGnssVelocity: {
      if (!gnss_enabled_) return false;
      info.dim = 3;
      info.H = Mat(3, n);
      for (int i = 0; i < 3; ++i) info.H(i, kIdxVel + i) = 1.0;
      info.y = ColVec3(m.v[0] - nominal.velocity_ned_mps.x, m.v[1] - nominal.velocity_ned_mps.y,
                       m.v[2] - nominal.velocity_ned_mps.z);
      const double s2 = config_.gnss_sigma_velocity_mps * config_.gnss_sigma_velocity_mps;
      info.R = blockIdentity(3, s2);
      break;
    }
    case MeasurementType::kBaroAltitude: {
      info.dim = 1;
      info.H = Mat(1, n);
      info.H(0, kIdxPos + 2) = -1.0;  // altitude = -pD
      info.y = Mat(1, 1);
      info.y(0, 0) = m.v[0] - (-nominal.position_ned_m.z);
      info.R = Mat(1, 1);
      info.R(0, 0) = config_.baro_sigma_m * config_.baro_sigma_m +
                     config_.baro_bias_uncertainty_m * config_.baro_bias_uncertainty_m;
      break;
    }
    case MeasurementType::kVisionRelative: {
      info.dim = 3;
      const double cpsi = std::cos(scene_.heading_rad);
      const double spsi = std::sin(scene_.heading_rad);
      const Vec3 d = nominal.position_ned_m - scene_.threshold_ned_m;
      const double lateral = -d.x * spsi + d.y * cpsi;
      const double along = d.x * cpsi + d.y * spsi;
      const double heading = wrapPi(nominal.attitude.yaw() - scene_.heading_rad);

      info.H = Mat(3, n);
      info.H(0, kIdxPos + 0) = -spsi;
      info.H(0, kIdxPos + 1) = cpsi;
      info.H(1, kIdxPos + 0) = cpsi;
      info.H(1, kIdxPos + 1) = spsi;
      // d(yaw)/d(dtheta_body) = third row of R, exact to first order for the
      // local attitude error convention used here.
      const Mat R = nominal.attitude.toRotationMatrix();
      info.H(2, kIdxAtt + 0) = R(2, 0);
      info.H(2, kIdxAtt + 1) = R(2, 1);
      info.H(2, kIdxAtt + 2) = R(2, 2);

      info.y = ColVec3(m.v[0] - lateral, m.v[1] - along, wrapPi(m.v[2] - heading));
      // SENS-010: the reported quality inflates R rather than being ignored.
      const double q = m.quality > 0.05 ? m.quality : 0.05;
      const double inflate = 1.0 / (q * q);
      info.R = Mat(3, 3);
      info.R(0, 0) = config_.vision_sigma_lateral_m * config_.vision_sigma_lateral_m * inflate;
      info.R(1, 1) =
          config_.vision_sigma_longitudinal_m * config_.vision_sigma_longitudinal_m * inflate;
      info.R(2, 2) = config_.vision_sigma_heading_rad * config_.vision_sigma_heading_rad * inflate;
      break;
    }
    case MeasurementType::kImuSample:
    case MeasurementType::kPseudorange:
      return false;  // IMU is an input; pseudoranges are handled by the RAIM module
  }

  const Mat Ht = info.H.transpose();
  info.S = info.H * P * Ht + info.R;
  info.S.symmetrize();
  double nis = 0.0;
  if (!quadraticFormInv(info.S, info.y, nis)) {
    info.valid = false;
    info.rejection_hint = IntegrityReason::kInnovationCovarianceInvalid;
    return true;  // model built, but the statistic is unusable
  }
  info.nis = nis;
  info.valid = true;
  return true;
}

bool ErrorStateEkf::prepareUpdate(const Measurement& m, InnovationInfo& info) {
  info = InnovationInfo{};
  if (!m.usable()) return false;

  info.measurement_age_s = t_s_ - m.header.sample_time_s;

  // A delayed measurement is linearised about the state it actually refers to.
  // The snapshot cadence is the IMU rate, so the state used is at most half an
  // IMU period away from the true sample instant.
  const Nominal* nominal = &nominal_;
  const Mat* P = &P_;
  Snapshot picked;
  if (config_.enable_rollback && info.measurement_age_s > 1.5e-3 && !snapshots_.empty()) {
    if (m.header.sample_time_s < snapshots_.front().t_s) {
      info.valid = false;
      info.rejection_hint = IntegrityReason::kMeasurementStale;
      return true;
    }
    for (auto it = snapshots_.rbegin(); it != snapshots_.rend(); ++it) {
      if (it->t_s <= m.header.sample_time_s) {
        picked = *it;
        nominal = &picked.nominal;
        P = &picked.P;
        break;
      }
    }
  }
  if (info.measurement_age_s > config_.max_measurement_age_s) {
    info.valid = false;
    info.rejection_hint = IntegrityReason::kMeasurementStale;
    return true;
  }
  return buildMeasurementModel(m, *nominal, *P, info);
}

void ErrorStateEkf::commit(const Measurement& m, const InnovationInfo& info) {
  const Mat Ht = info.H.transpose();
  Mat S_inv;
  if (!inverseSPD(info.S, S_inv)) {
    healthy_ = false;
    diagnostic_ = "innovation covariance not invertible for " + std::string(toString(m.type));
    return;
  }
  const Mat K = P_ * Ht * S_inv;
  const Mat dx = K * info.y;

  nominal_.position_ned_m += Vec3(dx(kIdxPos + 0, 0), dx(kIdxPos + 1, 0), dx(kIdxPos + 2, 0));
  nominal_.velocity_ned_mps += Vec3(dx(kIdxVel + 0, 0), dx(kIdxVel + 1, 0), dx(kIdxVel + 2, 0));
  const Vec3 dtheta(dx(kIdxAtt + 0, 0), dx(kIdxAtt + 1, 0), dx(kIdxAtt + 2, 0));
  nominal_.attitude = (nominal_.attitude * Quat::FromRotationVector(dtheta)).normalized();
  nominal_.accel_bias_mps2 +=
      Vec3(dx(kIdxAccelBias + 0, 0), dx(kIdxAccelBias + 1, 0), dx(kIdxAccelBias + 2, 0));
  nominal_.gyro_bias_radps +=
      Vec3(dx(kIdxGyroBias + 0, 0), dx(kIdxGyroBias + 1, 0), dx(kIdxGyroBias + 2, 0));

  // Joseph form: preserves symmetry and positive definiteness far better than
  // (I - K H) P under accumulated rounding (NAV-010).
  const Mat I = Mat::Identity(kStateDim);
  const Mat IKH = I - K * info.H;
  P_ = IKH * P_ * IKH.transpose() + K * info.R * K.transpose();
  P_.symmetrize();

  if (m.type == MeasurementType::kGnssPosition || m.type == MeasurementType::kVisionRelative) {
    last_absolute_fix_s_ = m.header.sample_time_s;
  }
  checkHealth();
}

void ErrorStateEkf::applyUpdate(const Measurement& m, const InnovationInfo& info) {
  if (!info.valid) return;

  const double age = t_s_ - m.header.sample_time_s;
  if (config_.enable_rollback && !replaying_ && age > 1.5e-3) {
    if (rollbackAndReplay(m)) return;
  }
  commit(m, info);
  if (!replaying_) {
    update_log_.push_back(m);
    trimBuffers();
  }
}

bool ErrorStateEkf::rollbackAndReplay(const Measurement& m) {
  if (snapshots_.empty() || m.header.sample_time_s < snapshots_.front().t_s) return false;

  const double target_t = t_s_;
  // Restore the newest snapshot at or before the measurement sample time.
  Snapshot base = snapshots_.front();
  for (const Snapshot& s : snapshots_) {
    if (s.t_s <= m.header.sample_time_s) {
      base = s;
    } else {
      break;
    }
  }
  nominal_ = base.nominal;
  P_ = base.P;
  t_s_ = base.t_s;
  last_absolute_fix_s_ = base.last_absolute_fix_s;

  // Collect everything that has to be replayed, in timestamp order.
  std::vector<Measurement> replay;
  for (const Measurement& im : imu_log_) {
    if (im.header.sample_time_s > base.t_s) replay.push_back(im);
  }
  for (const Measurement& up : update_log_) {
    if (up.header.sample_time_s > base.t_s) replay.push_back(up);
  }
  replay.push_back(m);
  std::stable_sort(replay.begin(), replay.end(), [](const Measurement& a, const Measurement& b) {
    if (a.header.sample_time_s != b.header.sample_time_s) {
      return a.header.sample_time_s < b.header.sample_time_s;
    }
    // IMU first at equal timestamps: propagate, then correct.
    const int ta = a.type == MeasurementType::kImuSample ? 0 : 1;
    const int tb = b.type == MeasurementType::kImuSample ? 0 : 1;
    if (ta != tb) return ta < tb;
    return a.header.sequence < b.header.sequence;
  });

  replaying_ = true;
  for (const Measurement& r : replay) {
    if (r.type == MeasurementType::kImuSample) {
      consumeImu(r);
    } else {
      InnovationInfo info;
      if (buildMeasurementModel(r, nominal_, P_, info) && info.valid) {
        commit(r, info);
      }
    }
  }
  if (t_s_ < target_t) propagateCovarianceOnly(target_t - t_s_);
  replaying_ = false;

  update_log_.push_back(m);
  // Snapshots taken before the rollback are stale for everything after
  // base.t_s; rebuild the head so the next rollback starts from valid data.
  while (!snapshots_.empty() && snapshots_.back().t_s > base.t_s) snapshots_.pop_back();
  pushSnapshot();
  trimBuffers();
  checkHealth();
  return true;
}

void ErrorStateEkf::pushSnapshot() {
  Snapshot s;
  s.t_s = t_s_;
  s.nominal = nominal_;
  s.P = P_;
  s.last_absolute_fix_s = last_absolute_fix_s_;
  snapshots_.push_back(s);
}

void ErrorStateEkf::trimBuffers() {
  const double horizon = t_s_ - config_.rollback_window_s;
  while (snapshots_.size() > 1 && snapshots_.front().t_s < horizon) snapshots_.pop_front();
  while (!imu_log_.empty() && imu_log_.front().header.sample_time_s < horizon) imu_log_.pop_front();
  while (!update_log_.empty() && update_log_.front().header.sample_time_s < horizon) {
    update_log_.pop_front();
  }
}

void ErrorStateEkf::checkHealth() {
  // NAV-014: NaN or Inf anywhere invalidates the run.
  if (!nominal_.position_ned_m.isFinite() || !nominal_.velocity_ned_mps.isFinite() ||
      !nominal_.attitude.isFinite() || !P_.isFinite()) {
    healthy_ = false;
    if (diagnostic_.empty()) diagnostic_ = "non-finite state or covariance";
    return;
  }
  if (!P_.hasPositiveDiagonal()) {
    healthy_ = false;
    if (diagnostic_.empty()) diagnostic_ = "covariance diagonal lost positivity";
  }
}

NavSolution ErrorStateEkf::solution() const {
  NavSolution s;
  s.t_s = t_s_;
  s.position_ned_m = nominal_.position_ned_m;
  s.velocity_ned_mps = nominal_.velocity_ned_mps;
  s.attitude_body_to_ned = nominal_.attitude;
  s.accel_bias_mps2 = nominal_.accel_bias_mps2;
  s.gyro_bias_radps = nominal_.gyro_bias_radps;
  s.position_covariance_m2 = P_.block(kIdxPos, kIdxPos, 3, 3);
  s.valid = healthy_;
  s.last_absolute_fix_age_s = t_s_ - last_absolute_fix_s_;

  if (!healthy_) {
    s.mode = NavMode::kUnsafe;
  } else if (mode_override_ != NavMode::kInitializing) {
    s.mode = mode_override_;
  } else if (s.last_absolute_fix_age_s > 5.0) {
    s.mode = NavMode::kDeadReckoning;
  } else {
    s.mode = NavMode::kNormal;
  }
  return s;
}

}  // namespace aerolab
