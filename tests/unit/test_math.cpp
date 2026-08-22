// VNV-001, VNV-011 - linear algebra, attitude conventions, chi-square.
#include <gtest/gtest.h>

#include <cmath>

#include "aerolab/integrity/chi_square.hpp"
#include "aerolab/math/matrix.hpp"
#include "aerolab/math/quaternion.hpp"
#include "aerolab/math/vec3.hpp"

using namespace aerolab;

TEST(Vec3, ArithmeticAndProducts) {
  const Vec3 a(1.0, 2.0, 3.0);
  const Vec3 b(-4.0, 5.0, 6.0);
  EXPECT_DOUBLE_EQ((a + b).x, -3.0);
  EXPECT_DOUBLE_EQ((a - b).y, -3.0);
  EXPECT_DOUBLE_EQ(a.dot(b), -4.0 + 10.0 + 18.0);
  const Vec3 c = a.cross(b);
  // a x b must be orthogonal to both operands.
  EXPECT_NEAR(c.dot(a), 0.0, 1e-12);
  EXPECT_NEAR(c.dot(b), 0.0, 1e-12);
  EXPECT_NEAR(a.normalized().norm(), 1.0, 1e-15);
  EXPECT_TRUE(a.isFinite());
  EXPECT_FALSE(Vec3(std::nan(""), 0.0, 0.0).isFinite());
}

TEST(Matrix, MultiplyTransposeIdentity) {
  Mat a(2, 3);
  a(0, 0) = 1;
  a(0, 1) = 2;
  a(0, 2) = 3;
  a(1, 0) = 4;
  a(1, 1) = 5;
  a(1, 2) = 6;
  const Mat at = a.transpose();
  ASSERT_EQ(at.rows(), 3);
  ASSERT_EQ(at.cols(), 2);
  const Mat p = a * at;  // 2x2
  EXPECT_DOUBLE_EQ(p(0, 0), 1 + 4 + 9);
  EXPECT_DOUBLE_EQ(p(0, 1), 4 + 10 + 18);
  EXPECT_DOUBLE_EQ(p(1, 1), 16 + 25 + 36);

  const Mat i = Mat::Identity(3);
  const Mat back = a * i;
  for (int r = 0; r < 2; ++r) {
    for (int c = 0; c < 3; ++c) EXPECT_DOUBLE_EQ(back(r, c), a(r, c));
  }
}

TEST(Matrix, CholeskySolvesAndRejectsNonSpd) {
  Mat a(3, 3);
  a(0, 0) = 4;
  a(0, 1) = 1;
  a(0, 2) = 0.5;
  a(1, 0) = 1;
  a(1, 1) = 3;
  a(1, 2) = 0.2;
  a(2, 0) = 0.5;
  a(2, 1) = 0.2;
  a(2, 2) = 2;
  Mat inv;
  ASSERT_TRUE(inverseSPD(a, inv));
  const Mat product = a * inv;
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      EXPECT_NEAR(product(r, c), r == c ? 1.0 : 0.0, 1e-12);
    }
  }
  // A matrix with a negative eigenvalue must be refused rather than producing
  // silent nonsense: NAV-014 depends on this returning false.
  Mat bad = Mat::Identity(2);
  bad(0, 0) = -1.0;
  Mat out;
  EXPECT_FALSE(inverseSPD(bad, out));
}

TEST(Matrix, QuadraticFormMatchesManualNis) {
  Mat s(2, 2);
  s(0, 0) = 4.0;
  s(1, 1) = 9.0;
  Mat y(2, 1);
  y(0, 0) = 2.0;
  y(1, 0) = 3.0;
  double nis = 0.0;
  ASSERT_TRUE(quadraticFormInv(s, y, nis));
  // (2/2)^2 + (3/3)^2 = 2 exactly.
  EXPECT_NEAR(nis, 2.0, 1e-12);
}

TEST(Matrix, SymmetrizeAndDiagnostics) {
  Mat m = Mat::Identity(3);
  m(0, 1) = 0.4;
  m(1, 0) = 0.6;
  m.symmetrize();
  EXPECT_DOUBLE_EQ(m(0, 1), 0.5);
  EXPECT_DOUBLE_EQ(m(1, 0), 0.5);
  EXPECT_TRUE(m.hasPositiveDiagonal());
  m(2, 2) = -1e-9;
  EXPECT_FALSE(m.hasPositiveDiagonal());
}

TEST(Matrix, SkewMatchesCrossProduct) {
  const Vec3 v(0.3, -1.2, 4.4);
  const Vec3 w(2.0, 0.5, -3.0);
  const Mat s = skew(v);
  Mat wc(3, 1);
  wc(0, 0) = w.x;
  wc(1, 0) = w.y;
  wc(2, 0) = w.z;
  const Mat r = s * wc;
  const Vec3 expected = v.cross(w);
  EXPECT_NEAR(r(0, 0), expected.x, 1e-14);
  EXPECT_NEAR(r(1, 0), expected.y, 1e-14);
  EXPECT_NEAR(r(2, 0), expected.z, 1e-14);
}

TEST(Quaternion, EulerRoundTrip) {
  const double roll = 0.21, pitch = -0.35, yaw = 2.6;
  const Quat q = Quat::FromEulerZyx(roll, pitch, yaw);
  EXPECT_NEAR(q.roll(), roll, 1e-12);
  EXPECT_NEAR(q.pitch(), pitch, 1e-12);
  EXPECT_NEAR(q.yaw(), yaw, 1e-12);
  EXPECT_NEAR(q.norm(), 1.0, 1e-15);
}

TEST(Quaternion, RotationIsOrthonormalAndInvertible) {
  const Quat q = Quat::FromEulerZyx(0.4, 0.2, -1.1);
  const Mat r = q.toRotationMatrix();
  const Mat rtr = r.transpose() * r;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) EXPECT_NEAR(rtr(i, j), i == j ? 1.0 : 0.0, 1e-13);
  }
  const Vec3 v(3.0, -1.0, 0.5);
  const Vec3 back = q.rotateInverse(q.rotate(v));
  EXPECT_NEAR(back.x, v.x, 1e-13);
  EXPECT_NEAR(back.y, v.y, 1e-13);
  EXPECT_NEAR(back.z, v.z, 1e-13);
}

TEST(Quaternion, SmallRotationVectorMatchesExact) {
  const Vec3 rv(1e-9, -2e-9, 3e-9);
  const Quat small = Quat::FromRotationVector(rv);
  EXPECT_NEAR(small.norm(), 1.0, 1e-15);
  EXPECT_NEAR(small.x, 0.5e-9, 1e-18);
  // A 90 degree rotation about Down must map North onto East.
  const Quat ninety = Quat::FromRotationVector(Vec3(0.0, 0.0, kPi * 0.5));
  const Vec3 east = ninety.rotate(Vec3(1.0, 0.0, 0.0));
  EXPECT_NEAR(east.x, 0.0, 1e-12);
  EXPECT_NEAR(east.y, 1.0, 1e-12);
}

TEST(Angles, WrapPiIsIdempotentAndCentred) {
  EXPECT_NEAR(wrapPi(0.0), 0.0, 1e-15);
  EXPECT_NEAR(wrapPi(kPi), kPi, 1e-12);
  EXPECT_NEAR(wrapPi(kPi + 0.1), -kPi + 0.1, 1e-12);
  EXPECT_NEAR(wrapPi(-3.0 * kPi), kPi, 1e-12);
  for (double a = -20.0; a < 20.0; a += 0.37) {
    const double w = wrapPi(a);
    EXPECT_LE(w, kPi + 1e-12);
    EXPECT_GT(w, -kPi - 1e-12);
    EXPECT_NEAR(wrapPi(w), w, 1e-12);
  }
}

// INT-004: the gate must be derived from a stated probability of false alert.
// Cross-checked against published chi-square tables.
TEST(ChiSquare, QuantilesMatchPublishedTables) {
  EXPECT_NEAR(gateThreshold(0.05, 1), 3.8415, 1e-3);
  EXPECT_NEAR(gateThreshold(0.05, 2), 5.9915, 1e-3);
  EXPECT_NEAR(gateThreshold(0.05, 3), 7.8147, 1e-3);
  EXPECT_NEAR(gateThreshold(0.01, 3), 11.3449, 1e-3);
  EXPECT_NEAR(gateThreshold(0.001, 3), 16.2662, 1e-3);
  EXPECT_NEAR(gateThreshold(0.001, 6), 22.4577, 1e-3);
}

TEST(ChiSquare, CdfIsMonotoneAndBounded) {
  double previous = -1.0;
  for (double x = 0.0; x < 40.0; x += 0.25) {
    const double p = chiSquareCdf(x, 3);
    EXPECT_GE(p, previous);
    EXPECT_GE(p, 0.0);
    EXPECT_LE(p, 1.0);
    previous = p;
  }
  EXPECT_NEAR(chiSquareCdf(0.0, 3), 0.0, 1e-12);
  EXPECT_NEAR(chiSquareCdf(1e5, 3), 1.0, 1e-9);
}
