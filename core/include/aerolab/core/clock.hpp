// AEROLAB RESILIENCE - simulation clock.
//
// Requirements: SYS-002 (fixed step deterministic), SYS-015 (monotonic sim
// clock independent of wall clock), SIM-007 (10 ms default step).
//
// Time is derived as tick_count * dt rather than accumulated by repeated
// addition. Repeated addition of 0.01 drifts by ~1e-13 s over a 10 minute run
// and makes the sensor scheduling non reproducible across step counts.
#pragma once

#include <cstdint>

namespace aerolab {

class SimClock {
 public:
  explicit SimClock(double step_s = 0.01) : step_s_(step_s) {}

  void reset() { tick_ = 0; }

  void advance() { ++tick_; }

  double time_s() const { return static_cast<double>(tick_) * step_s_; }
  double step_s() const { return step_s_; }
  std::int64_t tick() const { return tick_; }

  void setStep(double step_s) { step_s_ = step_s; }

  // Time of tick n without mutating the clock.
  double timeAt(std::int64_t tick_index) const { return static_cast<double>(tick_index) * step_s_; }

 private:
  double step_s_;
  std::int64_t tick_{0};
};

}  // namespace aerolab
