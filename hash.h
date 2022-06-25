/**
 * @file hash.h
 * @brief MurmurHash
 * @version 0.1
 * @date 2022-06-12
 */

#ifndef __HASH_H__
#define __HASH_H__

#include <string>

inline int rol32(int in, int x) {
  int res;
  __asm__ __volatile__("rol  %%eax, %%cl" : "=a"(res) : "a"(in), "c"(x));
  return res;
}

inline int ror32(int in, int x) {
  int res;
  __asm__ __volatile__("ror  %%eax, %%cl" : "=a"(res) : "a"(in), "c"(x));
  return res;
}

inline uint32_t rol32(uint32_t in, int x) {
  uint32_t res;
  __asm__ __volatile__("rol  %%eax, %%cl" : "=a"(res) : "a"(in), "c"(x));
  return res;
}

inline uint32_t ror32(uint32_t in, int x) {
  uint32_t res;
  __asm__ __volatile__("ror  %%eax, %%cl" : "=a"(res) : "a"(in), "c"(x));
  return res;
}

inline int64_t rol64(int64_t in, int x) { return (in << x) | (in >> (64 - x)); }

inline int64_t ror64(int64_t in, int x) { return (in >> x) | (in << (64 - x)); }

inline uint64_t rol64(uint64_t in, int x) {
  return (in << x) | (in >> (64 - x));
}

inline uint64_t ror64(uint64_t in, int x) {
  return (in >> x) | (in << (64 - x));
}

class hash32 {
  int seed;

  constexpr inline uint32_t fmix32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
  }

public:
  hash32() : seed(0) {}
  hash32(int seed) : seed(seed) {}

  uint32_t operator()(uint32_t x) { return seed ^ ror32(x * 0x9e3779b1U, 24); }

  uint32_t operator()(const void *s, uint32_t len) {
    const uint8_t *data = (const uint8_t *)s;
    const int nblocks = len / 4;
    uint32_t h1 = seed;
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;
    const uint32_t *blocks = (const uint32_t *)(data + nblocks * 4);
    for (int i = -nblocks; i; i++) {
      uint32_t k1 = blocks[i];

      k1 *= c1;
      k1 = rol32(k1, 15);
      k1 *= c2;

      h1 ^= k1;
      h1 = rol32(h1, 13);
      h1 = h1 * 5 + 0xe6546b64;
    }
    const uint8_t *tail = (const uint8_t *)(data + nblocks * 4);
    uint32_t k1 = 0;
    switch (len & 3) {
    case 3:
      k1 ^= tail[2] << 16;
    case 2:
      k1 ^= tail[1] << 8;
    case 1:
      k1 ^= tail[0];
      k1 *= c1;
      k1 = rol32(k1, 15);
      k1 *= c2;
      h1 ^= k1;
    };
    h1 ^= len;
    h1 = fmix32(h1);
    return h1;
  }

  uint32_t operator()(std::string &s) { return (*this)(s.data(), s.size()); }
};

class hash64 {
  int seed;

public:
  hash64() : seed(0) {}
  hash64(int seed) : seed(seed) {}

  uint64_t operator()(uint64_t x) {
    return seed ^ ror64(x * 0x9e3779b97f4a7c15LU, 24);
  }

  uint64_t operator()(const void *s, uint32_t len) {
    const uint64_t m = 0xc6a4a7935bd1e995;
    const int r = 47;

    uint64_t h = seed ^ (len * m);

    const uint64_t *data = (const uint64_t *)s;
    const uint64_t *end = data + (len / 8);

    while (data != end) {
      uint64_t k = *data++;

      k *= m;
      k ^= k >> r;
      k *= m;

      h ^= k;
      h *= m;
    }

    const uint8_t *data2 = (const uint8_t *)data;

    switch (len & 7) {
    case 7:
      h ^= uint64_t(data2[6]) << 48;
    case 6:
      h ^= uint64_t(data2[5]) << 40;
    case 5:
      h ^= uint64_t(data2[4]) << 32;
    case 4:
      h ^= uint64_t(data2[3]) << 24;
    case 3:
      h ^= uint64_t(data2[2]) << 16;
    case 2:
      h ^= uint64_t(data2[1]) << 8;
    case 1:
      h ^= uint64_t(data2[0]);
      h *= m;
    };

    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    return h;
  }

  uint64_t operator()(std::string &s) { return (*this)(s.data(), s.size()); }
};

class hash128 {
  int seed;

  uint64_t fmix64(uint64_t k) {
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccd;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53;
    k ^= k >> 33;

    return k;
  }

public:
  hash128() : seed(0) {}
  hash128(int seed) : seed(seed) {}

  std::pair<uint64_t, uint64_t> operator()(const void *s, uint32_t len) {
    const uint8_t *data = (const uint8_t *)s;
    const int nblocks = len / 16;

    uint64_t h1 = seed;
    uint64_t h2 = seed;

    const uint64_t c1 = 0x87c37b91114253d5;
    const uint64_t c2 = 0x4cf5ad432745937f;

    //----------
    // body

    const uint64_t *blocks = (const uint64_t *)(data);

    for (int i = 0; i < nblocks; i++) {
      uint64_t k1 = blocks[i * 2 + 0];
      uint64_t k2 = blocks[i * 2 + 1];

      k1 *= c1;
      k1 = rol64(k1, 31);
      k1 *= c2;
      h1 ^= k1;

      h1 = rol64(h1, 27);
      h1 += h2;
      h1 = h1 * 5 + 0x52dce729;

      k2 *= c2;
      k2 = rol64(k2, 33);
      k2 *= c1;
      h2 ^= k2;

      h2 = rol64(h2, 31);
      h2 += h1;
      h2 = h2 * 5 + 0x38495ab5;
    }

    //----------
    // tail

    const uint8_t *tail = (const uint8_t *)(data + nblocks * 16);

    uint64_t k1 = 0;
    uint64_t k2 = 0;

    switch (len & 15) {
    case 15:
      k2 ^= ((uint64_t)tail[14]) << 48;
    case 14:
      k2 ^= ((uint64_t)tail[13]) << 40;
    case 13:
      k2 ^= ((uint64_t)tail[12]) << 32;
    case 12:
      k2 ^= ((uint64_t)tail[11]) << 24;
    case 11:
      k2 ^= ((uint64_t)tail[10]) << 16;
    case 10:
      k2 ^= ((uint64_t)tail[9]) << 8;
    case 9:
      k2 ^= ((uint64_t)tail[8]) << 0;
      k2 *= c2;
      k2 = rol64(k2, 33);
      k2 *= c1;
      h2 ^= k2;

    case 8:
      k1 ^= ((uint64_t)tail[7]) << 56;
    case 7:
      k1 ^= ((uint64_t)tail[6]) << 48;
    case 6:
      k1 ^= ((uint64_t)tail[5]) << 40;
    case 5:
      k1 ^= ((uint64_t)tail[4]) << 32;
    case 4:
      k1 ^= ((uint64_t)tail[3]) << 24;
    case 3:
      k1 ^= ((uint64_t)tail[2]) << 16;
    case 2:
      k1 ^= ((uint64_t)tail[1]) << 8;
    case 1:
      k1 ^= ((uint64_t)tail[0]) << 0;
      k1 *= c1;
      k1 = rol64(k1, 31);
      k1 *= c2;
      h1 ^= k1;
    };

    //----------
    // finalization

    h1 ^= len;
    h2 ^= len;

    h1 += h2;
    h2 += h1;

    h1 = fmix64(h1);
    h2 = fmix64(h2);

    h1 += h2;
    h2 += h1;

    return std::make_pair(h1, h2);
  }

  std::pair<uint64_t, uint64_t> operator()(std::string &s) {
    return (*this)(s.data(), s.size());
  }
};

#endif // __HASH_H__