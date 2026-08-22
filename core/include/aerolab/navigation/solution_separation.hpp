// AEROLAB RESILIENCE - solution separation architecture (NAV-F).
//
// DEVIATION DEV-005 (docs/deviations.md) - promoted from COULD to MUST.
// INT-022 lists solution separation / ARAIM-like study as a COULD, i.e. an
// optional educational extension. It is implemented in V1 as a first class
// architecture instead, for a technical reason rather than an ambition:
//
//   A chi-square gate on the innovation of a single filter is structurally
//   weak against a slow drift. As the spoofed position ramps away, the filter
//   state follows it. The innovation - the difference between the measurement
//   and the filter prediction - therefore stays small, and the NIS never
//   crosses its threshold. SCN-004 is built to expose exactly this. An
//   integrity architecture that only ever tests innovations would report a
//   clean result while the position error grows without bound, which is the
//   most misleading outcome this project could publish.
//
//   Solution separation compares the all-sources solution against a sub-filter
//   that excludes the suspected source. The sub-filter does not follow the
//   spoof, so the separation grows with the injected error instead of being
//   absorbed by it. For two optimal filters where the sub-filter uses a subset
//   of the measurements, the covariance of the difference is the difference of
//   the covariances:
//         cov(x_full - x_sub) = P_sub - P_full
//   which gives the test
//         T = d^T (P_sub - P_full)^-1 d,   d = p_full - p_sub
//   distributed as chi-square with 2 degrees of freedom in the horizontal
//   plane. This is the mechanism behind RAIM solution separation and the
//   baseline of ARAIM. No conformance to any standard is claimed here
//   (section 19.2); the implementation is educational and its assumptions are
//   listed in docs/methodology/integrity.md.
//
//   The cost is one extra 15-state filter, and NAV-B already proves the
//   GNSS-free propagation works. The benefit is the headline result of the
//   project: the detectability floor as a function of drift rate, measured for
//   both policies on the same seeds.
//
// Requirements: NAV-004, NAV-005, INT-021, INT-022 (promoted), M-05, M-08.
#pragma once

#include "aerolab/navigation/error_state_ekf.hpp"

namespace aerolab {

class SolutionSeparationEstimator : public INavigationEstimator {
 public:
  SolutionSeparationEstimator()
      : main_(EstimatorId::kSolutionSeparation), sub_(EstimatorId::kSolutionSeparation) {
    sub_.setGnssEnabled(false);
  }

  void setScene(const RunwayScene& scene) {
    main_.setScene(scene);
    sub_.setScene(scene);
  }

  EstimatorId id() const override { return EstimatorId::kSolutionSeparation; }
  void initialize(const EstimatorConfig& config, const NavSolution& seed) override;
  void reset() override;
  void predict(double dt_s) override;
  void consumeImu(const Measurement& imu) override;
  bool prepareUpdate(const Measurement& m, InnovationInfo& info) override;
  void applyUpdate(const Measurement& m, const InnovationInfo& info) override;
  NavSolution solution() const override { return main_.solution(); }
  double time_s() const override { return main_.time_s(); }
  bool healthy() const override { return main_.healthy() && sub_.healthy(); }
  std::string diagnostic() const override;
  Mat covariance() const override { return main_.covariance(); }
  bool subSolution(NavSolution& out) const override {
    out = sub_.solution();
    return true;
  }

  // Horizontal separation test. Returns false when the covariance difference
  // is not usable, in which case the caller must not treat the absence of a
  // statistic as a pass.
  bool horizontalSeparation(double& statistic, double& separation_m, int& dof) const;

  void setModeOverride(NavMode mode) { main_.setModeOverride(mode); }
  void clearModeOverride() { main_.clearModeOverride(); }

  // Calibrated on the tuning seed set; see the comment in
  // horizontalSeparation() and docs/methodology/integrity.md.
  void setCovarianceInflation(double factor) { covariance_inflation_ = factor; }
  double covarianceInflation() const { return covariance_inflation_; }

  ErrorStateEkf& mainFilter() { return main_; }
  ErrorStateEkf& subFilter() { return sub_; }

 private:
  static bool isGnss(const Measurement& m) {
    return m.type == MeasurementType::kGnssPosition || m.type == MeasurementType::kGnssVelocity;
  }

  ErrorStateEkf main_;
  ErrorStateEkf sub_;
  double covariance_inflation_{1.0};
};

}  // namespace aerolab
