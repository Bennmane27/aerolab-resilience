#include "aerolab/integrity/raim.hpp"

#include <cmath>

#include "aerolab/integrity/chi_square.hpp"

namespace aerolab {

void RaimMonitor::configure(const std::vector<SyntheticSatellite>& satellites, double sigma_m,
                            double probability_false_alert) {
  geometry_.clear();
  for (const SyntheticSatellite& s : satellites) geometry_.push_back(s.lineOfSightNed());
  sigma_m_ = sigma_m > 0.0 ? sigma_m : 1.0;
  probability_false_alert_ = probability_false_alert;
  dof_full_ = static_cast<int>(geometry_.size()) - 4;
  threshold_full_ =
      dof_full_ > 0 ? gateThreshold(probability_false_alert_, dof_full_ > 6 ? 6 : dof_full_) : 0.0;
}

bool RaimMonitor::leastSquares(const std::vector<double>& ranges, const std::vector<int>& used,
                               Mat& x_out, double& sse_out) const {
  const int n = static_cast<int>(used.size());
  if (n < 4) return false;
  Mat G(n, 4);
  Mat z(n, 1);
  for (int i = 0; i < n; ++i) {
    const Vec3& e = geometry_[static_cast<std::size_t>(used[static_cast<std::size_t>(i)])];
    G(i, 0) = -e.x;
    G(i, 1) = -e.y;
    G(i, 2) = -e.z;
    G(i, 3) = 1.0;
    z(i, 0) = ranges[static_cast<std::size_t>(used[static_cast<std::size_t>(i)])];
  }
  const Mat Gt = G.transpose();
  const Mat N = Gt * G;
  const Mat b = Gt * z;
  Mat x;
  if (!solveSPD(N, b, x)) return false;
  const Mat r = z - G * x;
  double sse = 0.0;
  for (int i = 0; i < n; ++i) sse += r(i, 0) * r(i, 0);
  x_out = x;
  sse_out = sse;
  return true;
}

RaimResult RaimMonitor::evaluate(const std::vector<double>& ranges) const {
  RaimResult out;
  const std::size_t n = geometry_.size();
  if (ranges.size() != n || n < 5) return out;  // no redundancy, no statement

  std::vector<int> all;
  for (std::size_t i = 0; i < n; ++i) all.push_back(static_cast<int>(i));
  Mat x;
  double sse = 0.0;
  if (!leastSquares(ranges, all, x, sse)) return out;

  out.computed = true;
  out.sse = sse;
  out.degrees_of_freedom = static_cast<int>(n) - 4;
  out.statistic = sse / (sigma_m_ * sigma_m_);
  out.threshold = threshold_full_;
  out.detected = out.statistic > out.threshold;

  // A crude horizontal accuracy proxy from the geometry, reported so that the
  // scenario report shows the DOP effect. It is NOT a protection level.
  Mat G(static_cast<int>(n), 4);
  for (std::size_t i = 0; i < n; ++i) {
    const Vec3& e = geometry_[i];
    G(static_cast<int>(i), 0) = -e.x;
    G(static_cast<int>(i), 1) = -e.y;
    G(static_cast<int>(i), 2) = -e.z;
    G(static_cast<int>(i), 3) = 1.0;
  }
  Mat cov;
  if (inverseSPD(G.transpose() * G, cov)) {
    out.horizontal_protection_proxy_m = sigma_m_ * std::sqrt(cov(0, 0) + cov(1, 1)) * 5.33;
  }

  if (!out.detected || n < 6) return out;

  // Leave-one-out exclusion.
  double best_sse = 0.0;
  int best_index = -1;
  for (std::size_t drop = 0; drop < n; ++drop) {
    std::vector<int> subset;
    for (std::size_t i = 0; i < n; ++i) {
      if (i != drop) subset.push_back(static_cast<int>(i));
    }
    Mat xs;
    double sse_s = 0.0;
    if (!leastSquares(ranges, subset, xs, sse_s)) continue;
    if (best_index < 0 || sse_s < best_sse) {
      best_sse = sse_s;
      best_index = static_cast<int>(drop);
    }
  }
  if (best_index >= 0) {
    const int dof = static_cast<int>(n) - 1 - 4;
    const double threshold =
        dof > 0 ? gateThreshold(probability_false_alert_, dof > 6 ? 6 : dof) : 0.0;
    if (best_sse / (sigma_m_ * sigma_m_) <= threshold) {
      out.excluded = true;
      out.excluded_satellite = best_index;
    }
  }
  return out;
}

}  // namespace aerolab
