#include "aerolab/core/hash.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace aerolab {
namespace {

constexpr std::uint32_t kK[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

inline std::uint32_t rotr(std::uint32_t x, std::uint32_t n) {
  return (x >> n) | (x << (32u - n));
}

}  // namespace

Sha256::Sha256() : bit_length_(0), buffer_length_(0) {
  state_[0] = 0x6a09e667u;
  state_[1] = 0xbb67ae85u;
  state_[2] = 0x3c6ef372u;
  state_[3] = 0xa54ff53au;
  state_[4] = 0x510e527fu;
  state_[5] = 0x9b05688cu;
  state_[6] = 0x1f83d9abu;
  state_[7] = 0x5be0cd19u;
  std::memset(buffer_, 0, sizeof(buffer_));
}

void Sha256::transform(const unsigned char* chunk) {
  std::uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    const std::size_t o = static_cast<std::size_t>(i) * 4u;
    w[i] = (static_cast<std::uint32_t>(chunk[o]) << 24) |
           (static_cast<std::uint32_t>(chunk[o + 1]) << 16) |
           (static_cast<std::uint32_t>(chunk[o + 2]) << 8) |
           (static_cast<std::uint32_t>(chunk[o + 3]));
  }
  for (int i = 16; i < 64; ++i) {
    const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
  std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
  for (int i = 0; i < 64; ++i) {
    const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    const std::uint32_t ch = (e & f) ^ ((~e) & g);
    const std::uint32_t temp1 = h + s1 + ch + kK[i] + w[i];
    const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t temp2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }
  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

void Sha256::update(const void* data, std::size_t length) {
  const unsigned char* p = static_cast<const unsigned char*>(data);
  for (std::size_t i = 0; i < length; ++i) {
    buffer_[buffer_length_++] = p[i];
    if (buffer_length_ == 64) {
      transform(buffer_);
      bit_length_ += 512;
      buffer_length_ = 0;
    }
  }
}

std::string Sha256::hexDigest() {
  std::uint64_t total_bits = bit_length_ + static_cast<std::uint64_t>(buffer_length_) * 8u;
  std::size_t i = buffer_length_;
  buffer_[i++] = 0x80;
  if (i > 56) {
    while (i < 64) buffer_[i++] = 0;
    transform(buffer_);
    i = 0;
  }
  while (i < 56) buffer_[i++] = 0;
  for (int b = 7; b >= 0; --b) {
    buffer_[i++] = static_cast<unsigned char>((total_bits >> (b * 8)) & 0xffu);
  }
  transform(buffer_);

  char out[65];
  for (int k = 0; k < 8; ++k) {
    std::snprintf(out + k * 8, 9, "%08x", state_[static_cast<std::size_t>(k)]);
  }
  return std::string(out, 64);
}

std::string Sha256::ofString(const std::string& s) {
  Sha256 h;
  h.update(s);
  return h.hexDigest();
}

std::string Sha256::ofFile(const std::string& path, bool& ok) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    ok = false;
    return std::string();
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  ok = true;
  return ofString(ss.str());
}

void DeterminismHasher::feed(double value) {
  std::uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  unsigned char be[8];
  for (int i = 0; i < 8; ++i) be[i] = static_cast<unsigned char>((bits >> ((7 - i) * 8)) & 0xffu);
  sha_.update(be, 8);
}

void DeterminismHasher::feed(std::int64_t value) {
  const std::uint64_t bits = static_cast<std::uint64_t>(value);
  unsigned char be[8];
  for (int i = 0; i < 8; ++i) be[i] = static_cast<unsigned char>((bits >> ((7 - i) * 8)) & 0xffu);
  sha_.update(be, 8);
}

void DeterminismHasher::feed(const std::string& value) {
  sha_.update(value);
}

}  // namespace aerolab
