#ifndef __LFBITSET_H__
#define __LFBITSET_H__

#include <atomic>
#include <string.h>

class lfbitset_base {
  using uint64_t = unsigned long;

  uint64_t widx;
  uint64_t _Nbits;
  uint64_t align_N;
  bool self_alloc;
  volatile uint64_t *bitword;

public:
  lfbitset_base(uint64_t Nbits)
      : widx(0), _Nbits(Nbits), align_N((Nbits + 63) / 64), self_alloc(true) {
    bitword = new uint64_t[align_N];
    if (bitword == nullptr)
      throw "Out of Memory";
    reset();
  }
  lfbitset_base(volatile uint64_t *bitword, uint64_t Nbits)
      : bitword(bitword), widx(0), _Nbits(Nbits), align_N((Nbits + 63) / 64),
        self_alloc(false) {
    reset();
  }
  ~lfbitset_base() {
    if (self_alloc)
      delete bitword;
  }
  uint64_t capacity() const { return _Nbits; }
  void reset() {
    for (uint64_t i = 0; i < align_N; ++i) {
      bitword[i] = 0;
    }
  }
  bool none() const {
    for (uint64_t i = 0; i < align_N; ++i) {
      if (bitword[i] != 0)
        return false;
    }
    return true;
  }
  bool any() const {
    for (uint64_t i = 0; i < align_N; ++i) {
      if (bitword[i] != 0)
        return true;
    }
    return false;
  }
  bool nany() const {
    for (uint64_t i = 0; i < align_N - 1; ++i) {
      if (~bitword[i] != 0)
        return true;
    }
    uint64_t tail = _Nbits % 64;
    if (tail == 0) {
      return ~bitword[align_N - 1] != 0;
    } else {
      return bitword[align_N - 1] != (1L << tail) - 1;
    }
  }
  bool all() const {
    for (uint64_t i = 0; i < align_N - 1; ++i) {
      if (~bitword[i] != 0)
        return false;
    }
    uint64_t tail = _Nbits % 64;
    if (tail == 0) {
      return ~bitword[align_N - 1] == 0;
    } else {
      return bitword[align_N - 1] == (1L << tail) - 1;
    }
  }
  bool test(uint64_t __i) const {
    return bitword[__i / 64] & (1L << (__i % 64));
  }
  bool set(uint64_t __i) {
    uint64_t pre =
        __atomic_fetch_or(&bitword[__i / 64], 1L << (__i % 64),
                          __ATOMIC_RELAXED);
    return pre & (1L << (__i % 64));
  }
  bool reset(uint64_t __i) {
    uint64_t pre =
        __atomic_fetch_and(&bitword[__i / 64], ~(1L << (__i % 64)),
                           __ATOMIC_RELAXED);
    return pre & (1L << (__i % 64));
  }
  bool flip(uint64_t __i) {
    uint64_t pre =
        __atomic_fetch_xor(&bitword[__i / 64], 1L << (__i % 64),
                           __ATOMIC_RELAXED);
    return pre & (1L << (__i % 64));
  }
  uint64_t ffs_and_set() {
    uint64_t idx, widx = this->widx % align_N;
    for (uint64_t i = 0; i < align_N; ++i, widx = (widx + 1) % align_N) {
      uint64_t w = bitword[widx];
      while (1) {
        idx = ffsl(~w);
        if (idx == 0 || widx * 64 + idx > _Nbits) {
          break;
        }
        if (__atomic_compare_exchange_n(
                &bitword[widx], &w, w | (1L << (idx - 1)), true,
                __ATOMIC_SEQ_CST,
                __ATOMIC_SEQ_CST)) {
          this->widx = widx + 8;
          return widx * 64 + idx - 1;
        }
      }
    }
    return -1;
  }
  const uint64_t *to_ulong() { return (const uint64_t *)bitword; }

  void reset_bulk(uint64_t i, uint64_t n) {
    uint64_t s = i / 64, e = (i + n) / 64;
    if (s == e) {
      if (i % 64 == 0 && n == 64)
        bitword[s] = 0;
      else
        __atomic_fetch_and(&bitword[s], ~(((1UL << n) - 1UL) << (i % 64)),
                           __ATOMIC_RELAXED);
    } else {
      int h = 64 - (i % 64);
      if (h == 64)
        bitword[s] = 0;
      else
        __atomic_fetch_and(&bitword[s], ~(((1UL << h) - 1UL) << (i % 64)),
                           __ATOMIC_RELAXED);

      for (uint64_t m = s + 1; m < e; ++m) {
        bitword[m] = 0;
      }

      int t = (i + n) % 64;
      if (t == 64)
        bitword[e] = 0;
      else
        __atomic_fetch_and(&bitword[e], ~((1UL << t) - 1UL),
                           __ATOMIC_SEQ_CST);
    }
  }
};

template <unsigned long Nbits> class lfbitset : public lfbitset_base {
  using uint64_t = unsigned long;
  static const uint64_t align_N = (Nbits + 63) / 64;
  volatile uint64_t bitword[align_N];

public:
  lfbitset() : lfbitset_base(bitword, Nbits) {}
};

template <> class lfbitset<64> {
  using uint64_t = unsigned long;

  volatile uint64_t bitword;

public:
  lfbitset() { reset(); }

  uint64_t capacity() const { return 64; }

  void reset() { bitword = 0; }
  bool none() const { return bitword == 0; }
  bool any() const { return bitword != 0; }
  bool nany() const { return ~bitword != 0; }
  bool all() const { return ~bitword == 0; }
  bool test(uint64_t __i) const { return bitword & (1L << __i); }
  bool set(uint64_t __i) {
    uint64_t pre =
        __atomic_fetch_or(&bitword, 1L << __i,
                          __ATOMIC_RELAXED);
    return pre & (1L << __i);
  }
  bool reset(uint64_t __i) {
    uint64_t pre =
        __atomic_fetch_and(&bitword, ~(1L << __i),
                           __ATOMIC_RELAXED);
    return pre & (1L << __i);
  }
  bool flip(uint64_t __i) {
    uint64_t pre =
        __atomic_fetch_xor(&bitword, 1L << __i,
                           __ATOMIC_RELAXED);
    return pre & (1L << __i);
  }
  uint64_t ffs_and_set() {
    uint64_t idx;
    uint64_t w = bitword;
    while (1) {
      idx = ffsl(~w);
      if (idx == 0) {
        break;
      }
      if (__atomic_compare_exchange_n(
              &bitword, &w, w | (1L << (idx - 1)), true,
              __ATOMIC_SEQ_CST,
              __ATOMIC_SEQ_CST)) {
        return idx - 1;
      }
    }
    return -1;
  }

  const uint64_t *to_ulong() { return (const uint64_t *)&bitword; }

  uint64_t ffs_and_set_bulk(int n) {
    if (n == 1)
      return ffs_and_set();
    uint64_t w = bitword;
    uint64_t mask = (n == 64) ? ~0UL : ((1L << n) - 1);
    uint64_t idx = 0;
    while (idx <= 64 - n) {
      int first_0 = ffsl((~w) >> idx);
      if (first_0 == 0) {
        return -1;
      }
      int first_1_after_0 = ffsl(w >> (idx + first_0));
      if (first_1_after_0 != 0 && first_1_after_0 < n) {
        idx += first_0 + first_1_after_0;
      } else if (idx + first_0 - 1 > 64 - n) {
        return -1;
      } else if (__atomic_compare_exchange_n(
                     &bitword, &w, w | (mask << (idx + first_0 - 1)), true,
                     __ATOMIC_SEQ_CST,
                     __ATOMIC_SEQ_CST)) {
        return idx + first_0 - 1;
      } else {
        idx = 0;
      }
    }
    return -1;
  }

  void reset_bulk(int i, int n) {
    if (i == 0 && n == 64)
      bitword = 0;
    else
      __atomic_fetch_and(&bitword, ~(((1UL << n) - 1UL) << i),
                         __ATOMIC_RELAXED);
  }
};

#endif // __LFBITSET_H__