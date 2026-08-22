// AEROLAB RESILIENCE - unit quaternion and attitude conventions.
//
// CONVENTION (NAV-011, normative for the whole project):
//   * Quaternion q = (w, x, y, z), Hamilton product, scalar first.
//   * q rotates a vector from BODY to NED:  v_ned = q * v_body * q^-1.
//   * Euler sequence is ZYX (yaw, then pitch, then roll) applied intrinsically,
//     i.e. R = Rz(yaw) * Ry(pitch) * Rx(roll).
//   * Angles are radians everywhere in the core. Degrees appear only at the
//     configuration and UI boundaries and always carry a _deg suffix.
//   * yaw is wrapped to (-pi, pi]; pitch to [-pi/2, pi/2]; roll to (-pi, pi].
#pragma once

#include <cmath>

#include "aerolab/math/matrix.hpp"
#include "aerolab/math/vec3.hpp"

namespace aerolab {

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kDegToRad = kPi / 180.0;
inline constexpr double kRadToDeg = 180.0 / kPi;

// Wrap to (-pi, pi]. Deterministic and branch-stable.
inline double wrapPi(double angle_rad) {
  double a = std::fmod(angle_rad + kPi, 2.0 * kPi);
  if (a <= 0.0) a += 2.0 * kPi;
  return a - kPi;
}

struct Quat {
  double w{1.0};
  double x{0.0};
  double y{0.0};
  double z{0.0};

  constexpr Quat() = default;
  constexpr Quat(double w_in, double x_in, double y_in, double z_in)
      : w(w_in), x(x_in), y(y_in), z(z_in) {}

  static constexpr Quat Identity() { return Quat(1.0, 0.0, 0.0, 0.0); }

  static Quat FromEulerZyx(double roll_rad, double pitch_rad, double yaw_rad) {
    const double cr = std::cos(roll_rad * 0.5), sr = std::sin(roll_rad * 0.5);
    const double cp = std::cos(pitch_rad * 0.5), sp = std::sin(pitch_rad * 0.5);
    const double cy = std::cos(yaw_rad * 0.5), sy = std::sin(yaw_rad * 0.5);
    return Quat(cr * cp * cy + sr * sp * sy, sr * cp * cy - cr * sp * sy,
                cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy);
  }

  // Small-angle rotation vector (axis * angle, radians) to quaternion.
  // Uses the exact form; the series fallback keeps it finite at theta -> 0.
  static Quat FromRotationVector(const Vec3& rv_rad) {
    const double theta = rv_rad.norm();
    if (theta < 1e-12) {
      return Quat(1.0, 0.5 * rv_rad.x, 0.5 * rv_rad.y, 0.5 * rv_rad.z).normalized();
    }
    const double half = 0.5 * theta;
    const double s = std::sin(half) / theta;
    return Quat(std::cos(half), rv_rad.x * s, rv_rad.y * s, rv_rad.z * s);
  }

  Quat operator*(const Quat& o) const {  // Hamilton product
    return Quat(w * o.w - x * o.x - y * o.y - z * o.z, w * o.x + x * o.w + y * o.z - z * o.y,
                w * o.y - x * o.z + y * o.w + z * o.x, w * o.z + x * o.y - y * o.x + z * o.w);
  }

  Quat conjugate() const { return Quat(w, -x, -y, -z); }

  double norm() const { return std::sqrt(w * w + x * x + y * y + z * z); }

  Quat normalized() const {
    const double n = norm();
    if (!(n > 0.0)) return Identity();
    return Quat(w / n, x / n, y / n, z / n);
  }

  void normalize() { *this = normalized(); }

  bool isFinite() const {
    return std::isfinite(w) && std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
  }

  // Rotation matrix body -> NED.
  Mat toRotationMatrix() const {
    Mat r(3, 3);
    const double ww = w * w, xx = x * x, yy = y * y, zz = z * z;
    r(0, 0) = ww + xx - yy - zz;
    r(0, 1) = 2.0 * (x * y - w * z);
    r(0, 2) = 2.0 * (x * z + w * y);
    r(1, 0) = 2.0 * (x * y + w * z);
    r(1, 1) = ww - xx + yy - zz;
    r(1, 2) = 2.0 * (y * z - w * x);
    r(2, 0) = 2.0 * (x * z - w * y);
    r(2, 1) = 2.0 * (y * z + w * x);
    r(2, 2) = ww - xx - yy + zz;
    return r;
  }

  Vec3 rotate(const Vec3& v_body) const {
    const Mat r = toRotationMatrix();
    return Vec3(r(0, 0) * v_body.x + r(0, 1) * v_body.y + r(0, 2) * v_body.z,
                r(1, 0) * v_body.x + r(1, 1) * v_body.y + r(1, 2) * v_body.z,
                r(2, 0) * v_body.x + r(2, 1) * v_body.y + r(2, 2) * v_body.z);
  }

  Vec3 rotateInverse(const Vec3& v_ned) const { return conjugate().rotate(v_ned); }

  double roll() const { return std::atan2(2.0 * (w * x + y * z), 1.0 - 2.0 * (x * x + y * y)); }

  double pitch() const {
    double s = 2.0 * (w * y - z * x);
    if (s > 1.0) s = 1.0;
    if (s < -1.0) s = -1.0;
    return std::asin(s);
  }

  double yaw() const { return std::atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z)); }
};

// Rotation about the Down axis by psi (yaw), body -> NED for a level frame.
inline Mat rotationZ(double psi_rad) {
  const double c = std::cos(psi_rad), s = std::sin(psi_rad);
  Mat r(3, 3);
  r(0, 0) = c;
  r(0, 1) = -s;
  r(1, 0) = s;
  r(1, 1) = c;
  r(2, 2) = 1.0;
  return r;
}

}  // namespace aerolab
