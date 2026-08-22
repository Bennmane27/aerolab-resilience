// AEROLAB RESILIENCE - content hashing for manifests and determinism proofs.
//
// Requirements: DATA-005 (manifest carries scenario hash and config hash),
// M-15 (determinism hash of replay-relevant output), AT-001, API-008.
//
// Scope note: the determinism hash is an INTRA-BUILD contract. Two runs of the
// same binary with the same scenario and seed must produce the same hash. It is
// NOT expected to match across compilers, optimisation levels or the wasm
// target - see docs/deviations.md DEV-003 and the tolerance-based parity check
// used by AT-009 instead.
#pragma once

#include <cstdint>
#include <string>

namespace aerolab {

// SHA-256, FIPS 180-4. Used for scenario/config content hashes.
class Sha256 {
 public:
  Sha256();
  void update(const void* data, std::size_t length);
  void update(const std::string& s) { update(s.data(), s.size()); }
  std::string hexDigest();  // finalises; the object must not be reused after

  static std::string ofString(const std::string& s);
  static std::string ofFile(const std::string& path, bool& ok);

 private:
  void transform(const unsigned char* chunk);

  std::uint32_t state_[8];
  std::uint64_t bit_length_;
  unsigned char buffer_[64];
  std::size_t buffer_length_;
};

// Streaming hash of the numeric outputs of a run (M-15).
// Doubles are folded through their exact IEEE-754 bit pattern so that the hash
// is sensitive to the last bit, which is the whole point of the check.
class DeterminismHasher {
 public:
  void feed(double value);
  void feed(std::int64_t value);
  void feed(const std::string& value);
  std::string hexDigest() { return sha_.hexDigest(); }

 private:
  Sha256 sha_;
};

}  // namespace aerolab
