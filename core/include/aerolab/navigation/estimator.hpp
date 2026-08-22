// AEROLAB RESILIENCE - navigation estimator interface (subsystem S4).
//
// Requirements: NAV-005..NAV-016, SYS-014, INT-005, INT-010, INT-011.
//
// DEVIATION DEV-004 (docs/deviations.md) - split update, integrity gates BEFORE
// fusion. Section 5.2 of the cahier des charges orders the tick as
//     estimator.update(measurements)
//     integrity.evaluate(estimator, measurements)
// which runs the integrity check AFTER the measurement has already been fused.
// For NAV-D that is the wrong order: a spoofed GNSS fix would be in the state
// by the time the policy decides to reject it, and the innovation the policy
// inspects would be the post-update one. The interface below splits the update
// in two:
//     prepareUpdate()  -> computes y, S, H, R and the NIS. Changes no state.
//     <integrity decides>
//     applyUpdate()    -> commits the correction, only if accepted.
// This is also what makes INT-010 (monitor-only) and section 11.5 ("same
// estimator, different integrity policy") expressible at all.
#pragma once

#include <string>

#include "aerolab/core/types.hpp"
#include "aerolab/math/matrix.hpp"
#include "aerolab/math/quaternion.hpp"
#include "aerolab/sensors/measurement.hpp"
#include "aerolab/truth/truth_state.hpp"

namespace aerolab {

// NAV-012: every estimator publishes an uncertainty, not just a point.
struct NavSolution {
  double t_s{0.0};
  Vec3 position_ned_m{};
  Vec3 velocity_ned_mps{};
  Quat attitude_body_to_ned{};
  Vec3 accel_bias_mps2{};
  Vec3 gyro_bias_radps{};
  // Position covariance block, 3x3, NED [m^2]. Full state covariance is
  // available from the EKF variants through covariance().
  Mat position_covariance_m2{};
  NavMode mode{NavMode::kInitializing};
  bool valid{false};
  double last_absolute_fix_age_s{0.0};

  double horizontalSigma_m() const {
    if (position_covariance_m2.rows() < 2) return 0.0;
    return std::sqrt(position_covariance_m2(0, 0) + position_covariance_m2(1, 1));
  }
};

// Everything prepareUpdate() needs to hand to the integrity policy. Carrying H
// and R as well as y and S lets the policy re-derive the test it wants without
// reaching into the filter (INT-012: statistic, threshold and timestamp are all
// journaled from here).
struct InnovationInfo {
  bool valid{false};
  int dim{0};
  Mat y{};  // innovation, dim x 1
  Mat S{};  // innovation covariance, dim x dim
  Mat H{};  // measurement Jacobian, dim x n
  Mat R{};  // measurement noise, dim x dim
  double nis{0.0};
  double measurement_age_s{0.0};
  IntegrityReason rejection_hint{IntegrityReason::kNone};
};

// NAV-013: no Q, R or threshold is hard coded. Everything below comes from a
// versioned configuration file (configs/*.json).
struct EstimatorConfig {
  // Process noise (continuous densities, same units as the sensor models).
  double accel_noise_density_mps2_sqrthz{0.003};
  double gyro_noise_density_radps_sqrthz{0.0002};
  double accel_bias_walk_mps2_sqrts{1.0e-4};
  double gyro_bias_walk_radps_sqrts{1.0e-5};
  double position_process_noise_m_sqrts{0.0};

  // Measurement noise.
  double gnss_sigma_horizontal_m{2.0};
  double gnss_sigma_vertical_m{3.5};
  double gnss_sigma_velocity_mps{0.10};
  double baro_sigma_m{0.6};
  // The V1 state vector does not estimate the barometric bias, so the update
  // must account for it as extra measurement uncertainty. Documented in
  // docs/methodology/estimators.md as a known simplification (NFR-024).
  double baro_bias_uncertainty_m{3.0};
  double vision_sigma_lateral_m{0.8};
  double vision_sigma_longitudinal_m{3.0};
  double vision_sigma_heading_rad{0.5 * kDegToRad};

  // Initial covariance (simulated alignment quality).
  double init_sigma_position_m{8.0};
  double init_sigma_velocity_mps{0.5};
  double init_sigma_attitude_rad{1.0 * kDegToRad};
  double init_sigma_accel_bias_mps2{0.05};
  double init_sigma_gyro_bias_radps{0.002};

  // Latency policy (NAV-009, SCN-009).
  double max_measurement_age_s{2.5};
  bool enable_rollback{true};
  double rollback_window_s{2.5};

  // NAV-A only: how long the last GNSS fix stays usable.
  double gnss_only_timeout_s{2.0};
};

class INavigationEstimator {
 public:
  virtual ~INavigationEstimator() = default;

  virtual EstimatorId id() const = 0;
  virtual const char* name() const { return toString(id()); }

  // The seed solution stands in for an alignment phase. It is produced once,
  // before the run, by the runner - never read from the truth during the run
  // (NAV-006).
  virtual void initialize(const EstimatorConfig& config, const NavSolution& seed) = 0;

  // NAV-015: restores exactly the state initialize() left behind.
  virtual void reset() = 0;

  // Time propagation without an IMU sample (used by estimators that do not
  // consume inertial data, and to advance the published timestamp).
  virtual void predict(double dt_s) = 0;

  // Inertial propagation. IMU data is a control input, not a measurement.
  virtual void consumeImu(const Measurement& imu) = 0;

  // Computes the innovation for `m` WITHOUT modifying any state.
  // Returns false when the measurement is not usable by this estimator.
  virtual bool prepareUpdate(const Measurement& m, InnovationInfo& info) = 0;

  // Commits the correction. Only called when the integrity policy accepted it.
  virtual void applyUpdate(const Measurement& m, const InnovationInfo& info) = 0;

  virtual NavSolution solution() const = 0;

  // Internal filter time. The runner uses it to top up propagation when the
  // inertial stream is interrupted, so the covariance keeps growing even when
  // no IMU sample arrived this tick.
  virtual double time_s() const = 0;

  // NAV-014: a NaN or Inf in the state invalidates the run and dumps a
  // diagnostic instead of quietly producing numbers.
  virtual bool healthy() const = 0;
  virtual std::string diagnostic() const { return std::string(); }

  // Non-owning hint used by the integrity policy for cross checks; empty for
  // estimators that keep no covariance.
  virtual Mat covariance() const { return Mat(); }

  // Solution separation support (NAV-F). Returns false for estimators that do
  // not maintain a GNSS-free sub-filter.
  virtual bool subSolution(NavSolution& /*out*/) const { return false; }
};

}  // namespace aerolab
