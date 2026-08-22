// AEROLAB RESILIENCE - the two baselines (NAV-A, NAV-B).
//
// These exist to be beaten, and to make the failure modes legible:
//   NAV-A shows what happens when a single absolute source is trusted blindly:
//         a 100 m spoof is a 100 m position error, immediately, with no signal
//         that anything is wrong.
//   NAV-B shows what happens with no absolute source at all: bounded short
//         term accuracy, unbounded growth. It is the reference against which
//         "how long can we coast" is measured (SCN-002).
//
// Requirements: NAV-001, NAV-002, NAV-005, NAV-006, NAV-007, NAV-012, NAV-015.
#pragma once

#include <string>

#include "aerolab/navigation/estimator.hpp"

namespace aerolab {

// NAV-A: position is the most recent valid GNSS fix, propagated by nothing.
class GnssOnlyEstimator : public INavigationEstimator {
 public:
  EstimatorId id() const override { return EstimatorId::kGnssOnly; }
  void initialize(const EstimatorConfig& config, const NavSolution& seed) override;
  void reset() override;
  void predict(double dt_s) override { t_s_ += dt_s; }
  void consumeImu(const Measurement&) override {}  // by construction, ignored
  bool prepareUpdate(const Measurement& m, InnovationInfo& info) override;
  void applyUpdate(const Measurement& m, const InnovationInfo& info) override;
  NavSolution solution() const override;
  double time_s() const override { return t_s_; }
  bool healthy() const override { return true; }

 private:
  EstimatorConfig config_{};
  NavSolution seed_{};
  double t_s_{0.0};
  bool has_fix_{false};
  double last_fix_s_{-1.0e9};
  Vec3 position_ned_m_{};
  Vec3 velocity_ned_mps_{};
};

// NAV-B: strapdown inertial integration, no absolute update ever.
// The bias estimate is whatever the alignment handed over and never improves,
// which is precisely the behaviour that makes the drift visible.
class InsDeadReckoningEstimator : public INavigationEstimator {
 public:
  EstimatorId id() const override { return EstimatorId::kInsDeadReckoning; }
  void initialize(const EstimatorConfig& config, const NavSolution& seed) override;
  void reset() override;
  void predict(double dt_s) override;
  void consumeImu(const Measurement& imu) override;
  bool prepareUpdate(const Measurement&, InnovationInfo&) override { return false; }
  void applyUpdate(const Measurement&, const InnovationInfo&) override {}
  NavSolution solution() const override;
  double time_s() const override { return t_s_; }
  bool healthy() const override { return healthy_; }
  std::string diagnostic() const override { return diagnostic_; }

 private:
  void integrate(const Vec3& specific_force_body, const Vec3& angular_rate_body, double dt_s);

  EstimatorConfig config_{};
  NavSolution seed_{};
  double t_s_{0.0};
  Vec3 position_ned_m_{};
  Vec3 velocity_ned_mps_{};
  Quat attitude_{};
  Vec3 accel_bias_mps2_{};
  Vec3 gyro_bias_radps_{};
  Vec3 last_specific_force_body_{0.0, 0.0, -kGravityMps2};
  Vec3 last_angular_rate_body_{};
  // Growth model for the reported uncertainty: sigma_p grows as
  // sigma_v0 * t + 0.5 * sigma_a * t^2, which is the first order propagation of
  // the alignment error plus the residual accelerometer bias.
  double drift_time_s_{0.0};
  bool healthy_{true};
  std::string diagnostic_;
};

}  // namespace aerolab
