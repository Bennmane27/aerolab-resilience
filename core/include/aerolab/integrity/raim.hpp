// AEROLAB RESILIENCE - snapshot RAIM-like monitor (SCN-013, INT-021).
//
// Educational implementation of receiver autonomous integrity monitoring by
// residual test. NO conformance to any RAIM or ARAIM standard is claimed
// (section 19.2 of the cahier des charges); in particular there is no
// protection level computation, no integrity risk allocation, no constellation
// fault model and no continuity budget.
//
// Model
//   The synthetic pseudorange of satellite i is linearised about the local NED
//   origin: rho_i = -e_i . p + b + n_i, with e_i the unit line of sight and b
//   the receiver clock offset expressed in metres. The design matrix is
//   therefore G_i = [-e_ix, -e_iy, -e_iz, 1] and the unknown is x = (p, b).
//
// Test
//   Least squares solution x_hat = (G^T G)^-1 G^T rho, residual r = rho - G
//   x_hat, and the sum of squared errors SSE = |r|^2. Under the fault free
//   hypothesis SSE / sigma^2 follows a chi-square with n - 4 degrees of
//   freedom. Above the threshold, exclusion is by leave-one-out: the satellite
//   whose removal produces the smallest residual is the candidate, and it is
//   only excluded if the reduced set then passes its own test. With fewer than
//   6 satellites there is no redundancy to exclude with, and the monitor says
//   so instead of guessing.
#pragma once

#include <vector>

#include "aerolab/math/matrix.hpp"
#include "aerolab/sensors/sensor_suite.hpp"

namespace aerolab {

struct RaimResult {
  bool computed{false};
  bool detected{false};
  bool excluded{false};
  int excluded_satellite{-1};
  double sse{0.0};
  double statistic{0.0};  // SSE / sigma^2
  double threshold{0.0};
  int degrees_of_freedom{0};
  double horizontal_protection_proxy_m{0.0};  // not a certified HPL, see header
};

class RaimMonitor {
 public:
  void configure(const std::vector<SyntheticSatellite>& satellites, double sigma_m,
                 double probability_false_alert);

  // `ranges` must be one epoch, in satellite index order.
  RaimResult evaluate(const std::vector<double>& ranges) const;

  bool ready() const { return !geometry_.empty(); }

 private:
  bool leastSquares(const std::vector<double>& ranges, const std::vector<int>& used, Mat& x_out,
                    double& sse_out) const;

  std::vector<Vec3> geometry_;
  double sigma_m_{3.0};
  double probability_false_alert_{1.0e-5};
  double threshold_full_{0.0};
  int dof_full_{0};
};

}  // namespace aerolab
