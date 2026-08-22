#include "aerolab/sensors/sensor_suite.hpp"

#include <cmath>

namespace aerolab {

void SensorSuite::reset(const SensorSuiteConfig& config, std::uint64_t run_seed,
                        const RunwayScene& scene) {
  config_ = config;
  scene_ = scene;

  // SENS-011: one independent stream per sensor, all derived from the run seed.
  rng_gnss_pos_ = makeStream(run_seed, StreamId::kGnssPosition);
  rng_gnss_vel_ = makeStream(run_seed, StreamId::kGnssVelocity);
  rng_accel_ = makeStream(run_seed, StreamId::kImuAccel);
  rng_gyro_ = makeStream(run_seed, StreamId::kImuGyro);
  rng_accel_walk_ = makeStream(run_seed, StreamId::kImuAccelBiasWalk);
  rng_gyro_walk_ = makeStream(run_seed, StreamId::kImuGyroBiasWalk);
  rng_baro_ = makeStream(run_seed, StreamId::kBaro);
  rng_vision_ = makeStream(run_seed, StreamId::kVision);
  rng_pseudorange_ = makeStream(run_seed, StreamId::kPseudorange);

  accel_bias_mps2_ = config_.imu.accel_bias_initial_mps2;
  gyro_bias_radps_ = config_.imu.gyro_bias_initial_radps;

  gnss_index_ = 0;
  imu_index_ = 0;
  baro_index_ = 0;
  vision_index_ = 0;
  pseudorange_index_ = 0;
  gnss_sequence_ = 0;
  imu_sequence_ = 0;
  baro_sequence_ = 0;
  vision_sequence_ = 0;
  pseudorange_sequence_ = 0;

  // Frozen synthetic constellation. Azimuths spread evenly, elevations chosen
  // to give a well conditioned but not degenerate geometry (HDOP ~1.3).
  satellites_.clear();
  const int n = config_.pseudorange.satellite_count;
  for (int i = 0; i < n; ++i) {
    SyntheticSatellite s;
    const double frac = static_cast<double>(i) / static_cast<double>(n);
    s.azimuth_rad = wrapPi(2.0 * kPi * frac + 0.37);
    s.elevation_rad = (18.0 + 55.0 * std::fabs(std::sin(3.0 * kPi * frac + 0.9))) * kDegToRad;
    satellites_.push_back(s);
  }
}

void SensorSuite::sample(const TruthState& truth, double now_s, double dt_s,
                         std::vector<Measurement>& out) {
  // IMU first: it also integrates the bias random walk, which every other
  // sensor is independent of, but ordering must still be fixed for determinism.
  sampleImu(truth, now_s, dt_s, out);
  if (config_.gnss.enabled && config_.gnss.rate_hz > 0.0) {
    const double period = 1.0 / config_.gnss.rate_hz;
    const double next = static_cast<double>(gnss_index_) * period;
    if (due(next, now_s, dt_s)) {
      sampleGnss(truth, next, out);
      ++gnss_index_;
    }
  }
  if (config_.baro.enabled && config_.baro.rate_hz > 0.0) {
    const double period = 1.0 / config_.baro.rate_hz;
    const double next = static_cast<double>(baro_index_) * period;
    if (due(next, now_s, dt_s)) {
      sampleBaro(truth, next, out);
      ++baro_index_;
    }
  }
  if (config_.vision.enabled && config_.vision.rate_hz > 0.0) {
    const double period = 1.0 / config_.vision.rate_hz;
    const double next = static_cast<double>(vision_index_) * period;
    if (due(next, now_s, dt_s)) {
      sampleVision(truth, next, out);
      ++vision_index_;
    }
  }
  if (config_.pseudorange.enabled && config_.pseudorange.rate_hz > 0.0) {
    const double period = 1.0 / config_.pseudorange.rate_hz;
    const double next = static_cast<double>(pseudorange_index_) * period;
    if (due(next, now_s, dt_s)) {
      samplePseudorange(truth, next, out);
      ++pseudorange_index_;
    }
  }
}

void SensorSuite::sampleGnss(const TruthState& truth, double sample_time_s,
                             std::vector<Measurement>& out) {
  const GnssConfig& c = config_.gnss;

  Measurement pos;
  pos.type = MeasurementType::kGnssPosition;
  pos.dim = 3;
  pos.header.sensor = SensorId::kGnss;
  pos.header.sequence = gnss_sequence_;
  pos.header.sample_time_s = sample_time_s;
  pos.header.delivery_time_s = sample_time_s + c.latency_s;
  pos.v[0] = truth.position_ned_m.x + c.bias_position_ned_m.x +
             rng_gnss_pos_.nextGaussian() * c.sigma_position_horizontal_m;
  pos.v[1] = truth.position_ned_m.y + c.bias_position_ned_m.y +
             rng_gnss_pos_.nextGaussian() * c.sigma_position_horizontal_m;
  pos.v[2] = truth.position_ned_m.z + c.bias_position_ned_m.z +
             rng_gnss_pos_.nextGaussian() * c.sigma_position_vertical_m;
  pos.validateNumeric();
  out.push_back(pos);

  if (c.publish_velocity) {
    Measurement vel;
    vel.type = MeasurementType::kGnssVelocity;
    vel.dim = 3;
    vel.header.sensor = SensorId::kGnss;
    vel.header.sequence = gnss_sequence_;
    vel.header.sample_time_s = sample_time_s;
    vel.header.delivery_time_s = sample_time_s + c.latency_s;
    vel.v[0] = truth.velocity_ned_mps.x + rng_gnss_vel_.nextGaussian() * c.sigma_velocity_mps;
    vel.v[1] = truth.velocity_ned_mps.y + rng_gnss_vel_.nextGaussian() * c.sigma_velocity_mps;
    vel.v[2] = truth.velocity_ned_mps.z + rng_gnss_vel_.nextGaussian() * c.sigma_velocity_mps;
    vel.validateNumeric();
    out.push_back(vel);
  }
  ++gnss_sequence_;
}

void SensorSuite::sampleImu(const TruthState& truth, double now_s, double dt_s,
                            std::vector<Measurement>& out) {
  const ImuConfig& c = config_.imu;
  if (!c.enabled || c.rate_hz <= 0.0) return;
  const double period = 1.0 / c.rate_hz;
  const double next = static_cast<double>(imu_index_) * period;
  if (!due(next, now_s, dt_s)) return;

  // Bias random walk integrated at the IMU rate.
  const double sq = std::sqrt(period);
  accel_bias_mps2_.x += c.accel_bias_walk_mps2_sqrts * sq * rng_accel_walk_.nextGaussian();
  accel_bias_mps2_.y += c.accel_bias_walk_mps2_sqrts * sq * rng_accel_walk_.nextGaussian();
  accel_bias_mps2_.z += c.accel_bias_walk_mps2_sqrts * sq * rng_accel_walk_.nextGaussian();
  gyro_bias_radps_.x += c.gyro_bias_walk_radps_sqrts * sq * rng_gyro_walk_.nextGaussian();
  gyro_bias_radps_.y += c.gyro_bias_walk_radps_sqrts * sq * rng_gyro_walk_.nextGaussian();
  gyro_bias_radps_.z += c.gyro_bias_walk_radps_sqrts * sq * rng_gyro_walk_.nextGaussian();

  const double sigma_a = c.accel_noise_density_mps2_sqrthz * std::sqrt(c.rate_hz);
  const double sigma_g = c.gyro_noise_density_radps_sqrthz * std::sqrt(c.rate_hz);

  Measurement m;
  m.type = MeasurementType::kImuSample;
  m.dim = 6;
  m.header.sensor = SensorId::kImu;
  m.header.sequence = imu_sequence_++;
  m.header.sample_time_s = next;
  m.header.delivery_time_s = next + c.latency_s;
  m.v[0] =
      truth.specific_force_body_mps2.x + accel_bias_mps2_.x + sigma_a * rng_accel_.nextGaussian();
  m.v[1] =
      truth.specific_force_body_mps2.y + accel_bias_mps2_.y + sigma_a * rng_accel_.nextGaussian();
  m.v[2] =
      truth.specific_force_body_mps2.z + accel_bias_mps2_.z + sigma_a * rng_accel_.nextGaussian();
  m.v[3] =
      truth.angular_rate_body_radps.x + gyro_bias_radps_.x + sigma_g * rng_gyro_.nextGaussian();
  m.v[4] =
      truth.angular_rate_body_radps.y + gyro_bias_radps_.y + sigma_g * rng_gyro_.nextGaussian();
  m.v[5] =
      truth.angular_rate_body_radps.z + gyro_bias_radps_.z + sigma_g * rng_gyro_.nextGaussian();
  m.validateNumeric();
  out.push_back(m);
  ++imu_index_;
}

void SensorSuite::sampleBaro(const TruthState& truth, double sample_time_s,
                             std::vector<Measurement>& out) {
  const BaroConfig& c = config_.baro;
  Measurement m;
  m.type = MeasurementType::kBaroAltitude;
  m.dim = 1;
  m.header.sensor = SensorId::kBaro;
  m.header.sequence = baro_sequence_++;
  m.header.sample_time_s = sample_time_s;
  m.header.delivery_time_s = sample_time_s + c.latency_s;
  // SIM-011: derived from the truth altitude, independent of the GNSS vertical
  // channel, with its own slowly drifting bias.
  const double bias = c.bias_m + c.bias_drift_m_per_s * sample_time_s;
  m.v[0] = truth.altitude_m() + bias + c.sigma_m * rng_baro_.nextGaussian();
  m.validateNumeric();
  out.push_back(m);
}

void SensorSuite::sampleVision(const TruthState& truth, double sample_time_s,
                               std::vector<Measurement>& out) {
  const VisionConfig& c = config_.vision;
  Measurement m;
  m.type = MeasurementType::kVisionRelative;
  m.dim = 3;
  m.header.sensor = SensorId::kVision;
  m.header.sequence = vision_sequence_++;
  m.header.sample_time_s = sample_time_s;
  m.header.delivery_time_s = sample_time_s + c.latency_s;

  // Runway frame: u along the runway heading, w to the right of the centreline.
  const double cpsi = std::cos(scene_.heading_rad);
  const double spsi = std::sin(scene_.heading_rad);
  const Vec3 d = truth.position_ned_m - scene_.threshold_ned_m;
  const double along_m = d.x * cpsi + d.y * spsi;
  const double lateral_m = -d.x * spsi + d.y * cpsi;
  const double slant_m = std::sqrt(along_m * along_m + lateral_m * lateral_m);
  const double height_m = truth.altitude_m();

  const double bearing_rad = std::fabs(std::atan2(lateral_m, -along_m));
  const bool in_view = (-along_m) > 0.0 && slant_m < c.max_range_m && height_m < c.max_height_m &&
                       bearing_rad < c.half_field_of_view_rad;

  if (!in_view) {
    m.header.validity = Validity::kUnavailable;
    m.quality = 0.0;
    out.push_back(m);
    return;
  }

  // SENS-010 / SENS-018: quality degrades with range before the source is lost.
  double quality = 1.0;
  if (slant_m > c.quality_full_range_m) {
    quality = 1.0 - (slant_m - c.quality_full_range_m) / (c.max_range_m - c.quality_full_range_m);
  }
  if (quality < 0.0) quality = 0.0;
  if (quality > 1.0) quality = 1.0;
  // Noise inflates as quality falls; a quality of 0.25 quadruples sigma.
  const double inflation = 1.0 / (quality > 0.05 ? quality : 0.05);

  m.quality = quality;
  m.v[0] = lateral_m + c.sigma_lateral_m * inflation * rng_vision_.nextGaussian();
  m.v[1] = along_m + c.sigma_longitudinal_m * inflation * rng_vision_.nextGaussian();
  m.v[2] = wrapPi(truth.attitude_body_to_ned.yaw() - scene_.heading_rad) +
           c.sigma_heading_rad * inflation * rng_vision_.nextGaussian();
  m.validateNumeric();
  out.push_back(m);
}

void SensorSuite::samplePseudorange(const TruthState& truth, double sample_time_s,
                                    std::vector<Measurement>& out) {
  const PseudorangeConfig& c = config_.pseudorange;
  const double clock_m = c.clock_bias_m + c.clock_drift_m_per_s * sample_time_s;
  for (std::size_t i = 0; i < satellites_.size(); ++i) {
    const Vec3 e = satellites_[i].lineOfSightNed();
    Measurement m;
    m.type = MeasurementType::kPseudorange;
    m.dim = 1;
    m.header.sensor = SensorId::kGnssPseudorange;
    // Sequence encodes the satellite index in the low bits so a single fault
    // can target one satellite (SENS-020) without a separate field.
    m.header.sequence = pseudorange_sequence_ * 100u + static_cast<std::uint64_t>(i);
    m.header.sample_time_s = sample_time_s;
    m.header.delivery_time_s = sample_time_s + config_.gnss.latency_s;
    // Linearised range about the local origin: rho = R0 - e . p + b + noise.
    m.v[0] = -(e.x * truth.position_ned_m.x + e.y * truth.position_ned_m.y +
               e.z * truth.position_ned_m.z) +
             clock_m + c.sigma_m * rng_pseudorange_.nextGaussian();
    m.validateNumeric();
    out.push_back(m);
  }
  ++pseudorange_sequence_;
}

}  // namespace aerolab
