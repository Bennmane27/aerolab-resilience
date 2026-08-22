// AEROLAB RESILIENCE - chi-square distribution utilities.
//
// Requirements: INT-004 (gating thresholds configurable AND documented),
// M-12 (NIS consistency against the expected distribution).
//
// Thresholds are derived from a stated probability of false alert, not chosen
// as a round number of sigmas. A gate at "3 sigma" means nothing for a 3
// degree of freedom statistic; a gate at P_fa = 1e-3 with 3 dof means the
// nominal run should trip it once every thousand updates, which is a number
// the benchmark can actually verify (AT-003, INT-020).
//
// The regularized incomplete gamma function is implemented from its series and
// continued fraction expansions (Abramowitz & Stegun 6.5.29 / 6.5.31). Both
// converge to double precision within the iteration caps below over the range
// used here (dof 1..6, x up to ~60).
#pragma once

#include <cmath>

namespace aerolab {

namespace detail {

inline double gammaSeries(double a, double x) {
  const int kMaxIterations = 500;
  const double kEpsilon = 1.0e-15;
  double ap = a;
  double sum = 1.0 / a;
  double del = sum;
  for (int n = 0; n < kMaxIterations; ++n) {
    ap += 1.0;
    del *= x / ap;
    sum += del;
    if (std::fabs(del) < std::fabs(sum) * kEpsilon) break;
  }
  return sum * std::exp(-x + a * std::log(x) - std::lgamma(a));
}

inline double gammaContinuedFraction(double a, double x) {
  const int kMaxIterations = 500;
  const double kEpsilon = 1.0e-15;
  const double kTiny = 1.0e-300;
  double b = x + 1.0 - a;
  double c = 1.0 / kTiny;
  double d = 1.0 / b;
  double h = d;
  for (int i = 1; i <= kMaxIterations; ++i) {
    const double an = -static_cast<double>(i) * (static_cast<double>(i) - a);
    b += 2.0;
    d = an * d + b;
    if (std::fabs(d) < kTiny) d = kTiny;
    c = b + an / c;
    if (std::fabs(c) < kTiny) c = kTiny;
    d = 1.0 / d;
    const double del = d * c;
    h *= del;
    if (std::fabs(del - 1.0) < kEpsilon) break;
  }
  return std::exp(-x + a * std::log(x) - std::lgamma(a)) * h;
}

}  // namespace detail

// Lower regularized incomplete gamma P(a, x).
inline double regularizedGammaP(double a, double x) {
  if (x <= 0.0) return 0.0;
  if (x < a + 1.0) return detail::gammaSeries(a, x);
  return 1.0 - detail::gammaContinuedFraction(a, x);
}

// CDF of a chi-square distribution with `dof` degrees of freedom.
inline double chiSquareCdf(double x, int dof) {
  if (dof <= 0) return 0.0;
  return regularizedGammaP(0.5 * static_cast<double>(dof), 0.5 * x);
}

// Quantile such that P(X <= q) = probability. Bisection on a monotone CDF:
// slower than a rational approximation but exact to 1e-10 and, unlike a
// tabulated threshold, valid for any dof and any P_fa a scenario asks for.
// Called at configuration time only, never inside the tick loop.
inline double chiSquareQuantile(double probability, int dof) {
  if (dof <= 0) return 0.0;
  if (probability <= 0.0) return 0.0;
  if (probability >= 1.0) return 1.0e300;
  double lo = 0.0;
  double hi = 1.0;
  while (chiSquareCdf(hi, dof) < probability && hi < 1.0e6) hi *= 2.0;
  for (int i = 0; i < 200; ++i) {
    const double mid = 0.5 * (lo + hi);
    if (chiSquareCdf(mid, dof) < probability) {
      lo = mid;
    } else {
      hi = mid;
    }
    if (hi - lo < 1.0e-10 * (1.0 + hi)) break;
  }
  return 0.5 * (lo + hi);
}

// Convenience: the gate value for a stated probability of false alert.
inline double gateThreshold(double probability_false_alert, int dof) {
  return chiSquareQuantile(1.0 - probability_false_alert, dof);
}

}  // namespace aerolab
