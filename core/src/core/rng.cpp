#include "aerolab/core/rng.hpp"

#include <cmath>

namespace aerolab {

double Pcg32::nextGaussian() {
  if (has_spare_) {
    has_spare_ = false;
    return spare_;
  }
  // Marsaglia polar method. Rejection loop is deterministic given the stream.
  double u = 0.0;
  double v = 0.0;
  double s = 0.0;
  do {
    u = nextUniformSigned();
    v = nextUniformSigned();
    s = u * u + v * v;
  } while (s >= 1.0 || s == 0.0);
  const double factor = std::sqrt(-2.0 * std::log(s) / s);
  spare_ = v * factor;
  has_spare_ = true;
  return u * factor;
}

}  // namespace aerolab
