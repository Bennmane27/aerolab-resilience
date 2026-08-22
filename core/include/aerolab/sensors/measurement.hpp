// AEROLAB RESILIENCE - measurement record and the delivery-ordered bus.
//
// Requirements: SENS-001 (sensor_id, sequence, sample_time, delivery_time),
// SENS-013 (a late measurement keeps its original sample_time), SENS-014
// (NaN/Inf detected and marked before the estimator), NAV-008, NAV-009.
//
// DEVIATION DEV-002 (docs/deviations.md) - measurement bus, not a per-tick
// bundle. Section 5.2 of the cahier des charges models the tick as
//     injected = fault_engine.apply(sensors.sample(truth, t), t)
// i.e. one bundle of measurements per tick. That model cannot express
// SCN-009 (0.5 to 2.0 s of added latency): a measurement sampled at t and
// delivered at t + 1.5 s does not belong to tick t, and there is no slot for it
// in tick t + 150 either. The bus below is an event queue ordered by
// delivery_time. Sensors push at sample time, the fault engine may rewrite the
// delivery time, and the tick loop pops everything whose delivery time has
// come. Sample time is never modified, so the estimator can still tell how old
// a measurement is (SENS-013, INT-018).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "aerolab/core/types.hpp"

namespace aerolab {

struct MeasurementHeader {
  SensorId sensor{SensorId::kGnss};
  std::uint64_t sequence{0};
  double sample_time_s{0.0};    // when the physical quantity was observed
  double delivery_time_s{0.0};  // when the consumer can first see it
  Validity validity{Validity::kValid};
};

// Fixed payload, no allocation (NFR-002). Interpretation depends on `type`:
//   kGnssPosition   v[0..2] = N, E, D            [m],      dim 3
//   kGnssVelocity   v[0..2] = vN, vE, vD         [m/s],    dim 3
//   kImuSample      v[0..2] = specific force body [m/s^2]
//                   v[3..5] = angular rate body   [rad/s], dim 6
//   kBaroAltitude   v[0]    = altitude above field [m],    dim 1
//   kVisionRelative v[0] = lateral offset [m] (positive right of centreline)
//                   v[1] = longitudinal distance to threshold [m]
//                   v[2] = heading relative to runway [rad],  dim 3
//   kPseudorange    v[0]    = range [m],                    dim 1
struct Measurement {
  MeasurementHeader header{};
  MeasurementType type{MeasurementType::kGnssPosition};
  int dim{0};
  std::array<double, 6> v{};
  // SENS-010: quality is reported separately from the value itself, so an
  // integrity policy can react to degradation before the source disappears.
  double quality{1.0};

  double age_s(double now_s) const { return now_s - header.sample_time_s; }

  bool payloadIsFinite() const {
    for (int i = 0; i < dim; ++i) {
      if (!std::isfinite(v[static_cast<std::size_t>(i)])) return false;
    }
    return true;
  }

  // SENS-014: called at the sensor boundary; never silently drops the record.
  void validateNumeric() {
    if (header.validity == Validity::kValid && !payloadIsFinite()) {
      header.validity = Validity::kInvalidNumeric;
    }
  }

  bool usable() const { return header.validity == Validity::kValid; }
};

// Delivery-ordered measurement queue.
//
// Ordering is (delivery_time, sensor, sequence). The tie breakers matter: two
// sensors can be scheduled on the same tick and the resulting update order
// changes the filter output in the last bits. A total order that depends only
// on recorded fields keeps the run reproducible (SYS-004).
class MeasurementBus {
 public:
  void clear() { queue_.clear(); }

  void push(const Measurement& m) { queue_.push_back(m); }

  // Pops every measurement whose delivery time is <= now, in delivery order.
  void popDue(double now_s, std::vector<Measurement>& out) {
    out.clear();
    if (queue_.empty()) return;
    std::stable_sort(queue_.begin(), queue_.end(), less);
    std::size_t n = 0;
    while (n < queue_.size() && queue_[n].header.delivery_time_s <= now_s + kEpsilon_s) ++n;
    out.assign(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(n));
    queue_.erase(queue_.begin(), queue_.begin() + static_cast<std::ptrdiff_t>(n));
  }

  std::size_t pending() const { return queue_.size(); }

  // Largest delivery delay still queued; used to size the rollback buffer.
  double maxPendingDelay_s(double now_s) const {
    double worst = 0.0;
    for (const Measurement& m : queue_) {
      const double d = m.header.delivery_time_s - now_s;
      if (d > worst) worst = d;
    }
    return worst;
  }

 private:
  // Half a microsecond: far below any sensor period, far above the 1e-13 s
  // rounding of tick * dt.
  static constexpr double kEpsilon_s = 5.0e-7;

  static bool less(const Measurement& a, const Measurement& b) {
    if (a.header.delivery_time_s != b.header.delivery_time_s) {
      return a.header.delivery_time_s < b.header.delivery_time_s;
    }
    if (a.header.sensor != b.header.sensor) {
      return static_cast<int>(a.header.sensor) < static_cast<int>(b.header.sensor);
    }
    if (a.type != b.type) return static_cast<int>(a.type) < static_cast<int>(b.type);
    return a.header.sequence < b.header.sequence;
  }

  std::vector<Measurement> queue_;
};

}  // namespace aerolab
