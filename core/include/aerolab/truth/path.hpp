// AEROLAB RESILIENCE - horizontal ground track as a chain of constant
// curvature segments.
//
// Requirements: SIM-003 (straight approach), SIM-004 (stabilised turn),
// SIM-005 (taxi), SIM-008 (bounded speed and acceleration).
//
// A segment is either a straight line (curvature 0) or a circular arc
// (curvature k = 1/R, signed: positive turns right, i.e. towards increasing
// heading measured from North to East). Both admit an exact closed form in
// arc length s, so heading, position and curvature are analytic everywhere and
// no numerical integration drift can enter the ground truth.
//
//   heading(s) = h0 + k * s
//   k == 0 :  p(s) = p0 + s * (cos h0, sin h0)
//   k != 0 :  p(s) = p0 + (1/k) * (sin(h0 + k s) - sin h0,
//                                  cos h0 - cos(h0 + k s))
//
// Continuity: consecutive segments are chained on position and heading, so the
// track is C1 in arc length. Curvature steps at a segment boundary, which is a
// step in lateral acceleration - the same discontinuity a real aircraft feels
// when rolling into a turn. Profiles that must avoid it insert a roll-in
// segment; the V1 profiles accept it and it is documented in
// docs/methodology/truth.md.
#pragma once

#include <cmath>
#include <vector>

#include "aerolab/math/quaternion.hpp"

namespace aerolab {

struct PathPoint {
  double north_m{0.0};
  double east_m{0.0};
  double heading_rad{0.0};
  double curvature_per_m{0.0};
};

class HorizontalPath {
 public:
  void clear() { segments_.clear(); }

  void setStart(double north_m, double east_m, double heading_rad) {
    start_north_m_ = north_m;
    start_east_m_ = east_m;
    start_heading_rad_ = heading_rad;
  }

  void addStraight(double length_m) { segments_.push_back({length_m, 0.0}); }

  // Positive radius turns right (heading increases), negative turns left.
  void addArc(double radius_m, double sweep_rad) {
    const double k = 1.0 / radius_m;
    const double length = std::fabs(sweep_rad * radius_m);
    segments_.push_back({length, sweep_rad >= 0.0 ? std::fabs(k) : -std::fabs(k)});
  }

  double totalLength_m() const {
    double t = 0.0;
    for (const Segment& s : segments_) t += s.length_m;
    return t;
  }

  // Evaluate at arc length s. Beyond the last segment the track is extended
  // straight ahead so that long runs (SIM-006, 10 minutes) never fall off the
  // end of the definition.
  PathPoint evaluate(double s_m) const {
    double n = start_north_m_;
    double e = start_east_m_;
    double h = start_heading_rad_;
    double remaining = s_m;

    if (remaining < 0.0) {  // extend backwards along the initial heading
      return {n + remaining * std::cos(h), e + remaining * std::sin(h), h, 0.0};
    }
    for (const Segment& seg : segments_) {
      const double take = remaining < seg.length_m ? remaining : seg.length_m;
      advance(n, e, h, take, seg.curvature_per_m);
      remaining -= take;
      if (remaining <= 0.0) {
        return {n, e, wrapPi(h),
                take < seg.length_m || remaining == 0.0 ? seg.curvature_per_m : 0.0};
      }
    }
    // Past the end: straight extension.
    n += remaining * std::cos(h);
    e += remaining * std::sin(h);
    return {n, e, wrapPi(h), 0.0};
  }

  // Curvature blended across segment boundaries over `blend_length_m`.
  //
  // The raw curvature is piecewise constant, so it STEPS at every boundary. For
  // position and heading that is harmless. For the bank angle it is not: a
  // coordinated-turn bank is a function of curvature, so a curvature step means
  // an instantaneous roll from 0 to 20 degrees, i.e. an infinite roll rate. No
  // gyroscope can report that, so an estimator fed by this truth would inherit a
  // permanent 20 degree attitude error at the turn entry - which is exactly what
  // corrupted SCN-014 before this was added.
  //
  // Blending the curvature over a roll-in distance models what an aircraft
  // actually does: it rolls in over a couple of seconds. The consequence is that
  // the turn is not perfectly coordinated during roll-in (there is a brief
  // mismatch between bank and lateral acceleration), which is also what happens
  // in reality. See docs/methodology/truth.md.
  double curvatureSmoothed(double s_m, double blend_length_m) const {
    if (blend_length_m <= 0.0 || segments_.empty()) return evaluate(s_m).curvature_per_m;
    const double half = 0.5 * blend_length_m;
    double boundary = 0.0;
    double previous_curvature = 0.0;  // straight extension before the path
    for (std::size_t i = 0; i < segments_.size(); ++i) {
      const double start = boundary;
      const double end = boundary + segments_[i].length_m;
      const double k = segments_[i].curvature_per_m;
      if (s_m < start - half) return previous_curvature;
      if (s_m < start + half) {
        const double u = (s_m - (start - half)) / blend_length_m;
        return previous_curvature + (k - previous_curvature) * u * u * (3.0 - 2.0 * u);
      }
      if (s_m < end - half) return k;
      previous_curvature = k;
      boundary = end;
    }
    // Past the last segment the track extends straight ahead.
    const double u_end = (s_m - (boundary - half)) / blend_length_m;
    if (u_end >= 1.0) return 0.0;
    const double u = u_end < 0.0 ? 0.0 : u_end;
    return previous_curvature * (1.0 - u * u * (3.0 - 2.0 * u));
  }

  // Rigid translation applied after construction so that a chosen arc length
  // lands exactly on the runway threshold.
  void translateSoThat(double s_m, double target_north_m, double target_east_m) {
    const PathPoint p = evaluate(s_m);
    start_north_m_ += target_north_m - p.north_m;
    start_east_m_ += target_east_m - p.east_m;
  }

  bool empty() const { return segments_.empty(); }

 private:
  struct Segment {
    double length_m;
    double curvature_per_m;
  };

  static void advance(double& n, double& e, double& h, double ds, double k) {
    if (k == 0.0) {
      n += ds * std::cos(h);
      e += ds * std::sin(h);
    } else {
      const double h1 = h + k * ds;
      n += (std::sin(h1) - std::sin(h)) / k;
      e += (std::cos(h) - std::cos(h1)) / k;
      h = h1;
    }
  }

  std::vector<Segment> segments_;
  double start_north_m_{0.0};
  double start_east_m_{0.0};
  double start_heading_rad_{0.0};
};

}  // namespace aerolab
