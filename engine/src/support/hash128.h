// A 128-bit content hash (MurmurHash3 x64_128's block step over 8-byte
// words): the break memo's keys (plan P1-21) and a rendered block's key
// (plan P3-05). Not cryptographic; the keys name content within one
// process's caches.
#pragma once
#include <cstring>

#include "support.h"

namespace tsr {

struct Key128 {
  u64 lo = 0, hi = 0;
};

inline u64 rotl128(u64 x, int r) { return (x << r) | (x >> (64 - r)); }
inline u64 fmix128(u64 k) {  // MurmurHash3's finalizer
  k ^= k >> 33;
  k *= 0xFF51AFD7ED558CCDull;
  k ^= k >> 33;
  k *= 0xC4CEB9FE1A85EC53ull;
  k ^= k >> 33;
  return k;
}

struct Hasher {  // MurmurHash3 x64_128's block step over 8-byte words
  u64 h1 = 0x9E3779B97F4A7C15ull, h2 = 0xC2B2AE3D27D4EB4Full;
  u64 len = 0;
  void word(u64 k) {
    u64 k1 = k * 0x87C37B91114253D5ull;
    k1 = rotl128(k1, 31) * 0x4CF5AD432745937Full;
    h1 ^= k1;
    h1 = rotl128(h1, 27) + h2;
    h1 = h1 * 5 + 0x52DCE729;
    u64 k2 = (k ^ 0x5851F42D4C957F2Dull) * 0x4CF5AD432745937Full;
    k2 = rotl128(k2, 33) * 0x87C37B91114253D5ull;
    h2 ^= k2;
    h2 = rotl128(h2, 31) + h1;
    h2 = h2 * 5 + 0x38495AB5;
    len += 8;
  }
  void bytes(const void* p, size_t nb) {
    const unsigned char* c = (const unsigned char*)p;
    while (nb >= 8) {
      u64 k;
      std::memcpy(&k, c, 8);
      word(k);
      c += 8;
      nb -= 8;
    }
    if (nb) {
      u64 k = 0;
      std::memcpy(&k, c, nb);
      word(k ^ ((u64)nb << 56));
    }
  }
  void dbl(double v) {
    u64 b;
    std::memcpy(&b, &v, 8);
    word(b);
  }
  Key128 done() {
    h1 ^= len;
    h2 ^= len;
    h1 += h2;
    h2 += h1;
    h1 = fmix128(h1);
    h2 = fmix128(h2);
    h1 += h2;
    h2 += h1;
    return {h1, h2};
  }
};

inline bool operator==(const Key128& a, const Key128& b) { return a.lo == b.lo && a.hi == b.hi; }
struct Key128Hash {
  size_t operator()(const Key128& k) const { return (size_t)(k.lo ^ (k.hi * 0x9E3779B97F4A7C15ull)); }
};
// the key of a byte string
inline Key128 hash128(std::string_view s) {
  Hasher h;
  h.bytes(s.data(), s.size());
  return h.done();
}

}  // namespace tsr
