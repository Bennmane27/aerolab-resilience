// AEROLAB RESILIENCE - fixed-capacity dense matrix with runtime dimensions.
//
// Deviation DEV-001 (see docs/deviations.md): the cahier des charges section 9
// froze Eigen 3.x as the linear algebra layer. This project implements its own
// small dense matrix instead. Rationale:
//   * NFR-002 forbids dynamic allocation inside the simulation loop. This type
//     is entirely stack resident (kMax x kMax doubles, 2 KiB).
//   * Eigen selects different SIMD kernels on x86-64 (AVX2) and on wasm
//     (simd128 or scalar). Different reduction orders change rounding, which is
//     precisely the divergence AT-009 has to bound. A single scalar kernel
//     removes that variable from the native/wasm parity budget.
//   * The largest object in the project is the 15x15 EKF covariance. Eigen
//     expression templates and blocked kernels bring nothing at that size.
// Requirements: NAV-010, NFR-002, NFR-003.
#pragma once

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>

#include "aerolab/math/vec3.hpp"

namespace aerolab {

class Mat {
 public:
  static constexpr int kMax = 16;  // >= 15 (EKF error state) with headroom

  Mat() = default;
  Mat(int rows, int cols) : rows_(rows), cols_(cols) {
    assert(rows >= 0 && rows <= kMax && cols >= 0 && cols <= kMax);
    d_.fill(0.0);
  }

  static Mat Zero(int rows, int cols) { return Mat(rows, cols); }

  static Mat Identity(int n) {
    Mat m(n, n);
    for (int i = 0; i < n; ++i) m(i, i) = 1.0;
    return m;
  }

  int rows() const { return rows_; }
  int cols() const { return cols_; }

  double& operator()(int i, int j) {
    assert(i >= 0 && i < rows_ && j >= 0 && j < cols_);
    return d_[idx(i, j)];
  }
  double operator()(int i, int j) const {
    assert(i >= 0 && i < rows_ && j >= 0 && j < cols_);
    return d_[idx(i, j)];
  }

  void setZero() { d_.fill(0.0); }

  Mat operator+(const Mat& o) const {
    assert(rows_ == o.rows_ && cols_ == o.cols_);
    Mat r(rows_, cols_);
    for (int i = 0; i < rows_; ++i)
      for (int j = 0; j < cols_; ++j) r(i, j) = (*this)(i, j) + o(i, j);
    return r;
  }

  Mat operator-(const Mat& o) const {
    assert(rows_ == o.rows_ && cols_ == o.cols_);
    Mat r(rows_, cols_);
    for (int i = 0; i < rows_; ++i)
      for (int j = 0; j < cols_; ++j) r(i, j) = (*this)(i, j) - o(i, j);
    return r;
  }

  Mat operator*(double s) const {
    Mat r(rows_, cols_);
    for (int i = 0; i < rows_; ++i)
      for (int j = 0; j < cols_; ++j) r(i, j) = (*this)(i, j) * s;
    return r;
  }

  Mat operator*(const Mat& o) const {
    assert(cols_ == o.rows_);
    Mat r(rows_, o.cols_);
    for (int i = 0; i < rows_; ++i) {
      for (int k = 0; k < cols_; ++k) {
        const double a = (*this)(i, k);
        if (a == 0.0) continue;
        for (int j = 0; j < o.cols_; ++j) r(i, j) += a * o(k, j);
      }
    }
    return r;
  }

  Mat transpose() const {
    Mat r(cols_, rows_);
    for (int i = 0; i < rows_; ++i)
      for (int j = 0; j < cols_; ++j) r(j, i) = (*this)(i, j);
    return r;
  }

  // NAV-010: covariance matrices must stay symmetric under accumulated
  // floating point error. Called after every EKF update.
  void symmetrize() {
    for (int i = 0; i < rows_; ++i)
      for (int j = i + 1; j < cols_; ++j) {
        const double v = 0.5 * ((*this)(i, j) + (*this)(j, i));
        (*this)(i, j) = v;
        (*this)(j, i) = v;
      }
  }

  double trace() const {
    double t = 0.0;
    const int n = rows_ < cols_ ? rows_ : cols_;
    for (int i = 0; i < n; ++i) t += (*this)(i, i);
    return t;
  }

  bool isFinite() const {
    for (int i = 0; i < rows_; ++i)
      for (int j = 0; j < cols_; ++j)
        if (!std::isfinite((*this)(i, j))) return false;
    return true;
  }

  // NAV-010 diagnostic: every diagonal entry of a covariance must stay > 0.
  bool hasPositiveDiagonal() const {
    const int n = rows_ < cols_ ? rows_ : cols_;
    for (int i = 0; i < n; ++i)
      if (!((*this)(i, i) > 0.0)) return false;
    return true;
  }

  Mat block(int i0, int j0, int nrows, int ncols) const {
    Mat r(nrows, ncols);
    for (int i = 0; i < nrows; ++i)
      for (int j = 0; j < ncols; ++j) r(i, j) = (*this)(i0 + i, j0 + j);
    return r;
  }

  void setBlock(int i0, int j0, const Mat& b) {
    for (int i = 0; i < b.rows(); ++i)
      for (int j = 0; j < b.cols(); ++j) (*this)(i0 + i, j0 + j) = b(i, j);
  }

 private:
  static std::size_t idx(int i, int j) {
    return static_cast<std::size_t>(i) * static_cast<std::size_t>(kMax) +
           static_cast<std::size_t>(j);
  }
  int rows_{0};
  int cols_{0};
  std::array<double, static_cast<std::size_t>(kMax) * static_cast<std::size_t>(kMax)> d_{};
};

inline Mat operator*(double s, const Mat& m) {
  return m * s;
}

// --- Column vector helpers ---------------------------------------------------

inline Mat ColVec(int n) {
  return Mat(n, 1);
}

inline Mat ColVec3(double a, double b, double c) {
  Mat v(3, 1);
  v(0, 0) = a;
  v(1, 0) = b;
  v(2, 0) = c;
  return v;
}

inline Mat ColVec3(const Vec3& v) {
  return ColVec3(v.x, v.y, v.z);
}

inline Vec3 ToVec3(const Mat& m) {
  return Vec3(m(0, 0), m(1, 0), m(2, 0));
}

// --- Decompositions ----------------------------------------------------------

// Cholesky factorisation A = L L^T for symmetric positive definite A.
// Returns false when A is not numerically SPD; the caller must then treat the
// update as unusable rather than silently continuing (NAV-014).
inline bool cholesky(const Mat& a, Mat& l_out) {
  const int n = a.rows();
  if (n == 0 || n != a.cols()) return false;
  Mat l(n, n);
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j <= i; ++j) {
      double sum = a(i, j);
      for (int k = 0; k < j; ++k) sum -= l(i, k) * l(j, k);
      if (i == j) {
        if (!(sum > 0.0) || !std::isfinite(sum)) return false;
        l(i, j) = std::sqrt(sum);
      } else {
        l(i, j) = sum / l(j, j);
      }
    }
  }
  l_out = l;
  return true;
}

// Solve A X = B for symmetric positive definite A via Cholesky.
inline bool solveSPD(const Mat& a, const Mat& b, Mat& x_out) {
  Mat l;
  if (!cholesky(a, l)) return false;
  const int n = a.rows();
  const int m = b.cols();
  Mat y(n, m);
  for (int c = 0; c < m; ++c) {
    for (int i = 0; i < n; ++i) {  // forward substitution L y = b
      double sum = b(i, c);
      for (int k = 0; k < i; ++k) sum -= l(i, k) * y(k, c);
      y(i, c) = sum / l(i, i);
    }
    for (int i = n - 1; i >= 0; --i) {  // back substitution L^T x = y
      double sum = y(i, c);
      for (int k = i + 1; k < n; ++k) sum -= l(k, i) * y(k, c);
      y(i, c) = sum / l(i, i);
    }
  }
  x_out = y;
  return true;
}

inline bool inverseSPD(const Mat& a, Mat& inv_out) {
  return solveSPD(a, Mat::Identity(a.rows()), inv_out);
}

// Quadratic form v^T A^-1 v, used for the Normalized Innovation Squared
// (INT-003). Returns false when A is not usable.
inline bool quadraticFormInv(const Mat& a, const Mat& v, double& out) {
  Mat x;
  if (!solveSPD(a, v, x)) return false;
  double s = 0.0;
  for (int i = 0; i < v.rows(); ++i) s += v(i, 0) * x(i, 0);
  out = s;
  return std::isfinite(s);
}

// Skew symmetric matrix [v]x such that [v]x * w == v.cross(w).
inline Mat skew(const Vec3& v) {
  Mat m(3, 3);
  m(0, 1) = -v.z;
  m(0, 2) = v.y;
  m(1, 0) = v.z;
  m(1, 2) = -v.x;
  m(2, 0) = -v.y;
  m(2, 1) = v.x;
  return m;
}

}  // namespace aerolab
