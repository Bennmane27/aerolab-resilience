// AEROLAB RESILIENCE - 3-vector in a named physical frame.
// Requirements: NFR-005 (units in names), NFR-002 (no dynamic allocation).
#pragma once

#include <cmath>

namespace aerolab {

struct Vec3 {
  double x{0.0};
  double y{0.0};
  double z{0.0};

  constexpr Vec3() = default;
  constexpr Vec3(double x_in, double y_in, double z_in) : x(x_in), y(y_in), z(z_in) {}

  static constexpr Vec3 Zero() { return Vec3(0.0, 0.0, 0.0); }

  constexpr double operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
  double& at(int i) { return i == 0 ? x : (i == 1 ? y : z); }

  constexpr Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
  constexpr Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
  constexpr Vec3 operator-() const { return Vec3(-x, -y, -z); }
  constexpr Vec3 operator*(double s) const { return Vec3(x * s, y * s, z * s); }
  constexpr Vec3 operator/(double s) const { return Vec3(x / s, y / s, z / s); }

  Vec3& operator+=(const Vec3& o) {
    x += o.x;
    y += o.y;
    z += o.z;
    return *this;
  }
  Vec3& operator-=(const Vec3& o) {
    x -= o.x;
    y -= o.y;
    z -= o.z;
    return *this;
  }
  Vec3& operator*=(double s) {
    x *= s;
    y *= s;
    z *= s;
    return *this;
  }

  constexpr double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
  constexpr Vec3 cross(const Vec3& o) const {
    return Vec3(y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x);
  }
  double norm() const { return std::sqrt(dot(*this)); }
  constexpr double squaredNorm() const { return dot(*this); }
  Vec3 normalized() const {
    const double n = norm();
    return n > 0.0 ? (*this / n) : Vec3::Zero();
  }
  bool isFinite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
};

inline constexpr Vec3 operator*(double s, const Vec3& v) {
  return v * s;
}

}  // namespace aerolab
