// AEROLAB RESILIENCE - deterministic random number generation.
//
// Requirements: SYS-004 (explicit seed reproduces the same noise), SENS-011
// (each sensor owns an independent stream derived from the global seed),
// FI-016 (the fault engine is deterministic at identical seed/config).
//
// Design notes
//   * PCG32 (O'Neill 2014) is used rather than std::mt19937 because the C++
//     standard library distributions (std::normal_distribution and friends) are
//     NOT specified to produce identical sequences across implementations. The
//     libstdc++ and libc++/emscripten implementations genuinely differ, which
//     would break AT-009 before the numerics even get a chance to. Everything
//     here is specified bit-for-bit by this file alone.
//   * Gaussian sampling uses the Marsaglia polar method: it needs only log and
//     sqrt from libm, avoiding sin/cos whose last-bit behaviour differs the most
//     between glibc and the emscripten libm.
//   * Streams are addressed by a StreamId enum so that adding a sensor never
//     shifts the sequence consumed by an existing one.
#pragma once

#include <cstdint>

namespace aerolab {

// Stable stream identifiers. Values are part of the reproducibility contract:
// never renumber an existing entry, only append.
enum class StreamId : std::uint64_t {
  kGnssPosition = 1,
  kGnssVelocity = 2,
  kImuAccel = 3,
  kImuGyro = 4,
  kImuAccelBiasWalk = 5,
  kImuGyroBiasWalk = 6,
  kBaro = 7,
  kVision = 8,
  kVisionAvailability = 9,
  kFaultEngine = 10,
  kGnssDropout = 11,
  kPseudorange = 12,
  kEstimatorAuxiliary = 13,
};

// 64/32 permuted congruential generator, LCG variant.
class Pcg32 {
 public:
  Pcg32() { seed(0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL); }
  Pcg32(std::uint64_t initial_state, std::uint64_t stream_selector) {
    seed(initial_state, stream_selector);
  }

  void seed(std::uint64_t initial_state, std::uint64_t stream_selector) {
    state_ = 0U;
    inc_ = (stream_selector << 1U) | 1U;
    nextU32();
    state_ += initial_state;
    nextU32();
    has_spare_ = false;
    spare_ = 0.0;
  }

  std::uint32_t nextU32() {
    const std::uint64_t old = state_;
    state_ = old * 6364136223846793005ULL + inc_;
    const std::uint32_t xorshifted = static_cast<std::uint32_t>(((old >> 18U) ^ old) >> 27U);
    const std::uint32_t rot = static_cast<std::uint32_t>(old >> 59U);
    return (xorshifted >> rot) | (xorshifted << ((32U - rot) & 31U));
  }

  // Uniform on [0, 1), 53 significant bits.
  double nextUniform01() {
    const std::uint64_t hi = static_cast<std::uint64_t>(nextU32());
    const std::uint64_t lo = static_cast<std::uint64_t>(nextU32());
    const std::uint64_t bits = ((hi << 32) | lo) >> 11;  // 53 bits
    return static_cast<double>(bits) * (1.0 / 9007199254740992.0);
  }

  // Uniform on (-1, 1).
  double nextUniformSigned() { return 2.0 * nextUniform01() - 1.0; }

  double nextUniform(double lo, double hi) { return lo + (hi - lo) * nextUniform01(); }

  // Standard normal, Marsaglia polar method with a cached spare.
  double nextGaussian();

  double nextGaussian(double mean, double sigma) { return mean + sigma * nextGaussian(); }

  bool nextBernoulli(double probability) { return nextUniform01() < probability; }

 private:
  std::uint64_t state_{0};
  std::uint64_t inc_{1};
  bool has_spare_{false};
  double spare_{0.0};
};

// Mixes a global run seed with a stream identifier so that every stream is
// independent but fully determined by the run seed (SENS-011).
// SplitMix64 finaliser - fixed constants, part of the reproducibility contract.
inline std::uint64_t mixSeed(std::uint64_t run_seed, StreamId stream) {
  std::uint64_t z = run_seed + 0x9e3779b97f4a7c15ULL * (static_cast<std::uint64_t>(stream) + 1ULL);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

inline Pcg32 makeStream(std::uint64_t run_seed, StreamId stream) {
  return Pcg32(mixSeed(run_seed, stream), static_cast<std::uint64_t>(stream));
}

}  // namespace aerolab
