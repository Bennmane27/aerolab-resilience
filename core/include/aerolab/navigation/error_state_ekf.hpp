// AEROLAB RESILIENCE - 15 state error-state Kalman filter (NAV-C / NAV-D core).
//
// Requirements: NAV-003, NAV-004, NAV-005, NAV-008..NAV-016, INT-001..INT-003.
//
// Formulation
//   Nominal state  x = (p_ned, v_ned, q_body_to_ned, b_a, b_g)
//   Error state   dx = (dp, dv, dtheta, db_a, db_g), 15 components
//   The error is defined LOCALLY:  q_true = q_nominal (x) dq(dtheta),
//   dtheta expressed in body axes. This is the standard indirect / multiplicative
//   formulation. It is used instead of a direct Euler-angle EKF because the
//   Euler parameterisation is singular at pitch = +-90 deg and because the
//   covariance of a wrapped angle is not well defined near the wrap point.
//   See docs/methodology/estimators.md.
//
//   Continuous error dynamics (Sola, "Quaternion kinematics for the ESKF"):
//     d(dp)/dt     = dv
//     d(dv)/dt     = -R [a_b]x dtheta - R db_a + n_a
//     d(dtheta)/dt = -[w]x dtheta - db_g + n_g
//     d(db_a)/dt   = n_ba
//     d(db_g)/dt   = n_bg
//   with a_b = f_b - b_a and w = w_b - b_g the bias-compensated IMU signals.
//
//   Discretisation is first order, F = I + A dt. At 100 Hz the second order
//   term is ~5e-5 of the first; the unit test ekf_analytic checks that the
//   propagated covariance matches a fourth order reference over one second.
//
// Delayed measurements (NAV-009, SCN-009)
//   The filter keeps a snapshot per IMU sample over `rollback_window_s` and a
//   log of the updates it accepted. A measurement whose sample_time is older
//   than the filter time is fused by restoring the snapshot that brackets its
//   sample time and replaying the buffered inertial samples and accepted
//   updates forward. That is exact reprocessing, not an approximation, and it
//   is what makes a 2 s latency burst recoverable rather than merely
//   detectable. Measurements older than the buffer are refused with
//   kMeasurementStale.
#pragma once

#include <deque>
#include <string>
#include <vector>

#include "aerolab/navigation/estimator.hpp"
#include "aerolab/truth/truth_state.hpp"

namespace aerolab {

class ErrorStateEkf : public INavigationEstimator {
 public:
  static constexpr int kStateDim = 15;
  static constexpr int kIdxPos = 0;
  static constexpr int kIdxVel = 3;
  static constexpr int kIdxAtt = 6;
  static constexpr int kIdxAccelBias = 9;
  static constexpr int kIdxGyroBias = 12;

  explicit ErrorStateEkf(EstimatorId id = EstimatorId::kEkf) : id_(id) {}

  // The runway geometry is static scene data, not truth state: the vision
  // sensor reports offsets in this frame and the estimator needs the same
  // frame to build h(x). It contains no aircraft state.
  void setScene(const RunwayScene& scene) { scene_ = scene; }

  // Disables GNSS ingestion entirely. Used to build the GNSS-free sub-filter
  // of the solution separation architecture (NAV-F).
  void setGnssEnabled(bool enabled) { gnss_enabled_ = enabled; }

  EstimatorId id() const override { return id_; }
  void initialize(const EstimatorConfig& config, const NavSolution& seed) override;
  void reset() override;
  void predict(double dt_s) override;
  void consumeImu(const Measurement& imu) override;
  bool prepareUpdate(const Measurement& m, InnovationInfo& info) override;
  void applyUpdate(const Measurement& m, const InnovationInfo& info) override;
  NavSolution solution() const override;
  double time_s() const override { return t_s_; }
  bool healthy() const override { return healthy_; }
  std::string diagnostic() const override { return diagnostic_; }
  Mat covariance() const override { return P_; }

  // Exposed so the integrity policy can flag a source as isolated and the
  // filter can report DEAD_RECKONING rather than NORMAL.
  void setAbsoluteFixTime(double t_s) { last_absolute_fix_s_ = t_s; }
  double lastAbsoluteFixTime_s() const { return last_absolute_fix_s_; }
  void setModeOverride(NavMode mode) { mode_override_ = mode; }
  void clearModeOverride() { mode_override_ = NavMode::kInitializing; }

 private:
  struct Nominal {
    Vec3 position_ned_m{};
    Vec3 velocity_ned_mps{};
    Quat attitude{};
    Vec3 accel_bias_mps2{};
    Vec3 gyro_bias_radps{};
  };

  struct Snapshot {
    double t_s{0.0};
    Nominal nominal{};
    Mat P{};
    double last_absolute_fix_s{0.0};
  };

  void propagateWithImu(const Vec3& specific_force_body, const Vec3& angular_rate_body,
                        double dt_s);
  void propagateCovarianceOnly(double dt_s);
  bool buildMeasurementModel(const Measurement& m, const Nominal& nominal, const Mat& P,
                             InnovationInfo& info) const;
  void commit(const Measurement& m, const InnovationInfo& info);
  void pushSnapshot();
  bool rollbackAndReplay(const Measurement& m);
  void trimBuffers();
  void checkHealth();

  EstimatorId id_;
  EstimatorConfig config_{};
  RunwayScene scene_{};
  bool gnss_enabled_{true};

  Nominal nominal_{};
  Nominal initial_nominal_{};
  Mat P_{};
  Mat P_initial_{};
  double t_s_{0.0};
  double last_absolute_fix_s_{0.0};
  NavMode mode_override_{NavMode::kInitializing};

  Vec3 last_specific_force_body_{};
  Vec3 last_angular_rate_body_{};

  std::deque<Snapshot> snapshots_;
  std::deque<Measurement> imu_log_;
  std::deque<Measurement> update_log_;
  bool replaying_{false};

  bool healthy_{true};
  std::string diagnostic_;
};

}  // namespace aerolab
