// VNV-001, SYS-004, SENS-011 - deterministic random number generation.
#include <gtest/gtest.h>

#include <cmath>
#include <set>
#include <vector>

#include "aerolab/core/hash.hpp"
#include "aerolab/core/rng.hpp"

using namespace aerolab;

TEST(Rng, SameSeedReproducesTheSameSequence) {
  Pcg32 a(12345, 7);
  Pcg32 b(12345, 7);
  for (int i = 0; i < 1000; ++i) {
    EXPECT_EQ(a.nextU32(), b.nextU32());
  }
}

TEST(Rng, DifferentStreamsDiverge) {
  Pcg32 a(12345, 1);
  Pcg32 b(12345, 2);
  int identical = 0;
  for (int i = 0; i < 1000; ++i) {
    if (a.nextU32() == b.nextU32()) ++identical;
  }
  EXPECT_LT(identical, 5);  // collisions are possible, systematic equality is not
}

// SENS-011: every sensor stream must be independent yet fully determined by the
// run seed. Re-deriving a stream from the same run seed must reproduce it.
TEST(Rng, StreamsAreIndependentAndSeedDerived) {
  const std::uint64_t run_seed = 987654321ULL;
  Pcg32 gnss = makeStream(run_seed, StreamId::kGnssPosition);
  Pcg32 gnss_again = makeStream(run_seed, StreamId::kGnssPosition);
  Pcg32 imu = makeStream(run_seed, StreamId::kImuAccel);

  std::vector<double> a, b, c;
  for (int i = 0; i < 200; ++i) {
    a.push_back(gnss.nextGaussian());
    b.push_back(gnss_again.nextGaussian());
    c.push_back(imu.nextGaussian());
  }
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);

  // A different run seed must move every stream.
  Pcg32 other = makeStream(run_seed + 1, StreamId::kGnssPosition);
  EXPECT_NE(a[0], other.nextGaussian());
}

TEST(Rng, UniformIsInRangeAndUnbiased) {
  Pcg32 rng(42, 3);
  double sum = 0.0;
  double minimum = 2.0;
  double maximum = -1.0;
  const int n = 200000;
  for (int i = 0; i < n; ++i) {
    const double u = rng.nextUniform01();
    ASSERT_GE(u, 0.0);
    ASSERT_LT(u, 1.0);
    sum += u;
    minimum = std::min(minimum, u);
    maximum = std::max(maximum, u);
  }
  EXPECT_NEAR(sum / n, 0.5, 0.005);
  EXPECT_LT(minimum, 0.01);
  EXPECT_GT(maximum, 0.99);
}

TEST(Rng, GaussianHasUnitVarianceAndZeroMean) {
  Pcg32 rng(2024, 11);
  const int n = 400000;
  double sum = 0.0;
  double sum_sq = 0.0;
  double sum_fourth = 0.0;
  for (int i = 0; i < n; ++i) {
    const double g = rng.nextGaussian();
    sum += g;
    sum_sq += g * g;
    sum_fourth += g * g * g * g;
  }
  const double mean = sum / n;
  const double variance = sum_sq / n - mean * mean;
  const double kurtosis = sum_fourth / n / (variance * variance);
  EXPECT_NEAR(mean, 0.0, 0.01);
  EXPECT_NEAR(variance, 1.0, 0.02);
  EXPECT_NEAR(kurtosis, 3.0, 0.15);  // normal kurtosis
}

TEST(Rng, GaussianWithMeanAndSigma) {
  Pcg32 rng(7, 7);
  double sum = 0.0;
  double sum_sq = 0.0;
  const int n = 200000;
  for (int i = 0; i < n; ++i) {
    const double g = rng.nextGaussian(5.0, 2.0);
    sum += g;
    sum_sq += (g - 5.0) * (g - 5.0);
  }
  EXPECT_NEAR(sum / n, 5.0, 0.03);
  EXPECT_NEAR(std::sqrt(sum_sq / n), 2.0, 0.03);
}

TEST(Rng, BernoulliMatchesRequestedProbability) {
  Pcg32 rng(99, 4);
  int hits = 0;
  const int n = 100000;
  for (int i = 0; i < n; ++i) {
    if (rng.nextBernoulli(0.25)) ++hits;
  }
  EXPECT_NEAR(static_cast<double>(hits) / n, 0.25, 0.01);
}

TEST(Rng, ReseedResetsTheStreamCompletely) {
  Pcg32 rng(1, 1);
  const double first = rng.nextGaussian();
  for (int i = 0; i < 37; ++i) rng.nextGaussian();  // leave a cached spare behind
  rng.seed(1, 1);
  EXPECT_DOUBLE_EQ(rng.nextGaussian(), first);
}

// DATA-005 / M-15: the hashing used for manifests and determinism proofs.
TEST(Sha256, MatchesKnownVectors) {
  EXPECT_EQ(Sha256::ofString(""),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  EXPECT_EQ(Sha256::ofString("abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(Sha256::ofString("The quick brown fox jumps over the lazy dog"),
            "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592");
}

TEST(DeterminismHasher, IsSensitiveToTheLastBit) {
  DeterminismHasher a;
  DeterminismHasher b;
  a.feed(1.0);
  b.feed(1.0 + 1e-16);  // below double resolution at 1.0: identical value
  EXPECT_EQ(a.hexDigest(), b.hexDigest());

  DeterminismHasher c;
  DeterminismHasher d;
  c.feed(1.0);
  d.feed(std::nextafter(1.0, 2.0));  // one ulp apart: must differ
  EXPECT_NE(c.hexDigest(), d.hexDigest());
}
