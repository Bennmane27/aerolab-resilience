// AEROLAB RESILIENCE - synthetic sensor suite (subsystem S2).
//
// Requirements: SENS-001..SENS-020.
//
// Every sensor here is NOMINAL. Anything abnormal - loss, freeze, bias, spoof,
// latency, drop - belongs to the fault engine (FI-017). Keeping the split sharp
// is what makes AT-002 checkable: the sensor layer reads the truth, the fault
// layer never sees it.
//
// Noise parameterisation
//   IMU noise is given as a spectral density (m/s^2/sqrt(Hz), rad/s/sqrt(Hz)).
//   The discrete standard deviation at rate f is density * sqrt(f). Bias random
//   walk is given in units/sqrt(s) and integrated as b += sigma * sqrt(dt) * n.
//   GNSS, baro and vision noise are given directly as discrete standard
//   deviations because those sensors deliver already-filtered estimates.
#pragma once

#include <cstdint>
#include <vector>

#include "aerolab/core/rng.hpp"
#include "aerolab/sensors/measurement.hpp"
#include "aerolab/truth/truth_state.hpp"

namespace aerolab {

struct GnssConfig {
  bool enabled{true};
  double rate_hz{5.0};
  double sigma_position_horizontal_m{2.0};
  double sigma_position_vertical_m{3.5};
  double sigma_velocity_mps{0.10};
  Vec3 bias_position_ned_m{0.0, 0.0, 0.0};
  double latency_s{0.08};
  bool publish_velocity{true};
};

struct ImuConfig {
  bool enabled{true};
  double rate_hz{100.0};  // SENS-007: at least 100 Hz nominally
  double accel_noise_density_mps2_sqrthz{0.003};
  double gyro_noise_density_radps_sqrthz{0.0002};
  Vec3 accel_bias_initial_mps2{0.02, -0.015, 0.03};
  Vec3 gyro_bias_initial_radps{0.0005, -0.0004, 0.0006};
  double accel_bias_walk_mps2_sqrts{1.0e-4};
  double gyro_bias_walk_radps_sqrts{1.0e-5};
  double latency_s{0.0};
};

struct BaroConfig {
  bool enabled{true};
  double rate_hz{20.0};
  double sigma_m{0.6};
  double bias_m{1.5};  // residual QNH setting error
  double bias_drift_m_per_s{0.01};
  double latency_s{0.02};
};

struct VisionConfig {
  bool enabled{true};
  double rate_hz{20.0};
  double sigma_lateral_m{0.8};
  double sigma_longitudinal_m{3.0};
  double sigma_heading_rad{0.5 * kDegToRad};
  double max_range_m{3000.0};  // beyond this the runway is not resolvable
  double max_height_m{400.0};
  double half_field_of_view_rad{25.0 * kDegToRad};
  double latency_s{0.05};
  double quality_full_range_m{1500.0};  // quality = 1 below this distance
};

struct PseudorangeConfig {
  bool enabled{false};  // SENS-019 is SHOULD; enabled by SCN-013
  double rate_hz{1.0};
  int satellite_count{8};
  double sigma_m{3.0};
  double clock_bias_m{0.0};
  double clock_drift_m_per_s{0.0};
};

struct SensorSuiteConfig {
  GnssConfig gnss{};
  ImuConfig imu{};
  BaroConfig baro{};
  VisionConfig vision{};
  PseudorangeConfig pseudorange{};
};

// Synthetic satellite geometry: fixed azimuth/elevation, i.e. a frozen snapshot
// of a constellation. Enough to give the RAIM-like check a realistic DOP
// without pulling in an orbital model (SENS-019).
struct SyntheticSatellite {
  double azimuth_rad{0.0};
  double elevation_rad{0.0};
  Vec3 lineOfSightNed() const {
    const double ce = std::cos(elevation_rad);
    return Vec3(ce * std::cos(azimuth_rad), ce * std::sin(azimuth_rad),
                -std::sin(elevation_rad));  // Down negative => pointing up
  }
};

class SensorSuite {
 public:
  void reset(const SensorSuiteConfig& config, std::uint64_t run_seed, const RunwayScene& scene);

  // Generates every measurement whose sample instant falls in (previous tick,
  // now]. Pushes them on the bus with sample and delivery times already set.
  void sample(const TruthState& truth, double now_s, double dt_s, std::vector<Measurement>& out);

  const SensorSuiteConfig& config() const { return config_; }
  const std::vector<SyntheticSatellite>& satellites() const { return satellites_; }

  // Current true IMU bias, used only by tests and by the analysis tooling.
  Vec3 trueAccelBias_mps2() const { return accel_bias_mps2_; }
  Vec3 trueGyroBias_radps() const { return gyro_bias_radps_; }

 private:
  bool due(double next_time_s, double now_s, double dt_s) const {
    return next_time_s <= now_s + 0.5 * dt_s && next_time_s > now_s - 0.5 * dt_s;
  }

  void sampleGnss(const TruthState& truth, double now_s, std::vector<Measurement>& out);
  void sampleImu(const TruthState& truth, double now_s, double dt_s, std::vector<Measurement>& out);
  void sampleBaro(const TruthState& truth, double now_s, std::vector<Measurement>& out);
  void sampleVision(const TruthState& truth, double now_s, std::vector<Measurement>& out);
  void samplePseudorange(const TruthState& truth, double now_s, std::vector<Measurement>& out);

  SensorSuiteConfig config_{};
  RunwayScene scene_{};

  Pcg32 rng_gnss_pos_{};
  Pcg32 rng_gnss_vel_{};
  Pcg32 rng_accel_{};
  Pcg32 rng_gyro_{};
  Pcg32 rng_accel_walk_{};
  Pcg32 rng_gyro_walk_{};
  Pcg32 rng_baro_{};
  Pcg32 rng_vision_{};
  Pcg32 rng_pseudorange_{};

  Vec3 accel_bias_mps2_{};
  Vec3 gyro_bias_radps_{};

  std::int64_t gnss_index_{0};
  std::int64_t imu_index_{0};
  std::int64_t baro_index_{0};
  std::int64_t vision_index_{0};
  std::int64_t pseudorange_index_{0};

  std::uint64_t gnss_sequence_{0};
  std::uint64_t imu_sequence_{0};
  std::uint64_t baro_sequence_{0};
  std::uint64_t vision_sequence_{0};
  std::uint64_t pseudorange_sequence_{0};

  std::vector<SyntheticSatellite> satellites_{};
};

}  // namespace aerolab
