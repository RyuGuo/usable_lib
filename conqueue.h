/**
 * @file conqueue.h
 * @brief lock-free concurrent queue
 * @version 0.1
 * @date 2022-06-12
 */

#ifndef __CONQUEUE_H__
#define __CONQUEUE_H__

#include <atomic>
#include <cstdlib>
#include <emmintrin.h>
#include <iterator>
#include <sys/cdefs.h>
#include <thread>

enum ConQueueMode {
  F_SP_ENQ = 1,
  F_SC_DEQ = 2,
  F_EXACT_SZ = 4,
  F_MP_RTS_ENQ = 8,
  F_MC_RTS_DEQ = 16,
  F_MP_HTS_ENQ = 32,
  F_MC_HTS_DEQ = 64,
};
template <typename T, typename Alloc = std::allocator<T>> class ConQueue {
public:
  ConQueue(T *ring, uint32_t max_size,
           uint32_t flags = ConQueueMode::F_MP_HTS_ENQ |
                            ConQueueMode::F_MC_HTS_DEQ)
      : ring(ring), self_alloc(false), flags(flags), max_size(max_size) {
    prod_head.raw = prod_tail.raw = cons_head.raw = cons_tail.raw = 0;

    if (ring == nullptr)
      throw "ring array is null";
    if (max_size == 0)
      throw "max_size is 0";
    if ((this->flags & F_SP_ENQ) &&
        (this->flags & (F_MP_RTS_ENQ | F_MP_HTS_ENQ)))
      throw "queue producer flag error";
    if ((this->flags & F_MP_RTS_ENQ) && (this->flags & F_MP_HTS_ENQ))
      throw "queue producer flag error";
    if ((this->flags & F_SC_DEQ) &&
        (this->flags & (F_MC_RTS_DEQ | F_MC_HTS_DEQ)))
      throw "queue consumer flag error";
    if ((this->flags & F_MC_RTS_DEQ) && (this->flags & F_MC_HTS_DEQ))
      throw "queue consumer flag error";

    // default flag set
    if (!(this->flags & (F_SP_ENQ | F_MP_RTS_ENQ | F_MP_HTS_ENQ)))
      this->flags |= F_MP_HTS_ENQ;
    if (!(this->flags & (F_SC_DEQ | F_MC_RTS_DEQ | F_MC_HTS_DEQ)))
      this->flags |= F_MC_HTS_DEQ;

    count_mask = aligned_size(max_size, this->flags) - 1;
  }
  ConQueue(uint32_t max_size, uint32_t flags = ConQueueMode::F_MP_HTS_ENQ |
                                               ConQueueMode::F_MC_HTS_DEQ)
      : ConQueue(Alloc().allocate(aligned_size(max_size, flags)), max_size,
                 flags) {
    self_alloc = true;
  }
  ~ConQueue() {
    clear();
    if (self_alloc)
      Alloc().deallocate(ring, max_size);
  }
  uint32_t capacity() const { return max_size; }
  uint32_t size() const { return prod_tail.pos - cons_tail.pos; }
  bool empty() const { return prod_tail.pos == cons_tail.pos; }
  bool full() const { return prod_tail.pos - cons_tail.pos == capacity(); }
  // Guarantee that the reference will not be poped
  T &top() const { return ring[cons_tail.pos]; }
  void clear() {
    uint32_t _h = prod_tail.pos, _t = cons_tail.pos;
    for (uint32_t i = _t; i != _h; ++i) {
      ring[to_id(i)].~T();
    }
    prod_head.pos = prod_tail.pos = cons_head.pos = cons_tail.pos = 0;
  }
  bool push(const T &x) { return emplace(std::forward<const T &>(x)); }
  bool push(T &&x) { return emplace(std::forward<T>(x)); }
  template <typename... Args> bool emplace(Args &&...args) {
    if (flags & ConQueueMode::F_SP_ENQ) {
      if (__glibc_unlikely(prod_tail.pos - cons_tail.pos == capacity()))
        return false;
      new (&ring[to_id(prod_tail.pos)]) T(std::forward<Args>(args)...);
      ++prod_tail.pos;
    } else if (flags & ConQueueMode::F_MP_HTS_ENQ) {
      union po_val_t oh, nh;
      oh.raw = __atomic_load_n(&prod_head.raw, __ATOMIC_ACQUIRE);
      do {
        if (__glibc_unlikely(oh.pos - cons_tail.pos == capacity()))
          return false;
        nh.pos = oh.pos + 1;
      } while (!__atomic_compare_exchange_n(&prod_head.raw, &oh.raw, nh.raw,
                                            true, __ATOMIC_ACQUIRE,
                                            __ATOMIC_ACQUIRE));
      new (&ring[to_id(oh.pos)]) T(std::forward<Args>(args)...);
      hts_update_ht(&prod_tail, 1, oh.pos);
    } else {
      union po_val_t oh, nh;
      oh.raw = __atomic_load_n(&prod_head.raw, __ATOMIC_ACQUIRE);
      do {
        if (__glibc_unlikely(oh.pos - cons_tail.pos == capacity()))
          return false;
        nh.pos = oh.pos + 1;
        nh.cnt = oh.cnt + 1;
      } while (!__atomic_compare_exchange_n(&prod_head.raw, &oh.raw, nh.raw,
                                            true, __ATOMIC_ACQUIRE,
                                            __ATOMIC_ACQUIRE));
      new (&ring[to_id(oh.pos)]) T(std::forward<Args>(args)...);
      rts_update_ht(&prod_head, &prod_tail);
    }
    return true;
  }
  /**
   * @brief push several elements as much as possible
   *
   * @param first input first iterator
   * @param last input last iterator
   * @return The number of pushed elements
   */
  template <typename Iter> uint32_t push_bulk(Iter first, Iter last) {
    uint32_t l;
    uint32_t count = std::distance(first, last);
    if (flags & ConQueueMode::F_SP_ENQ) {
      l = std::min(capacity() - prod_tail.pos + cons_tail.pos, count);
      for (uint32_t i = 0; i < l; ++i) {
        new (&ring[to_id(prod_tail.pos + i)]) T(*(first++));
      }
      prod_tail.pos += l;
    } else if (flags & ConQueueMode::F_MP_RTS_ENQ) {
      union po_val_t oh, nh;
      oh.raw = __atomic_load_n(&prod_head.raw, __ATOMIC_ACQUIRE);
      do {
        l = std::min(capacity() - oh.pos + cons_tail.pos, count);
        if (l == 0)
          return 0;
        nh.pos = oh.pos + l;
      } while (!__atomic_compare_exchange_n(&prod_head.raw, &oh.raw, nh.raw,
                                            true, __ATOMIC_ACQUIRE,
                                            __ATOMIC_ACQUIRE));
      for (uint32_t i = 0; i < l; ++i) {
        new (&ring[to_id(oh.pos + i)]) T(*(first++));
      }
      hts_update_ht(&prod_tail, l, oh.pos);
    } else {
      union po_val_t oh, nh;
      oh.raw = __atomic_load_n(&prod_head.raw, __ATOMIC_ACQUIRE);
      do {
        l = std::min(capacity() - oh.pos + cons_tail.pos, count);
        if (l == 0)
          return 0;
        nh.pos = oh.pos + l;
        nh.cnt = oh.cnt + 1;
      } while (!__atomic_compare_exchange_n(&prod_head.raw, &oh.raw, nh.raw,
                                            true, __ATOMIC_ACQUIRE,
                                            __ATOMIC_ACQUIRE));
      for (uint32_t i = 0; i < l; ++i) {
        new (&ring[to_id(oh.pos + i)]) T(*(first++));
      }
      rts_update_ht(&prod_head, &prod_tail);
    }
    return l;
  }
  bool pop(T *x) {
    if (flags & ConQueueMode::F_SC_DEQ) {
      if (__glibc_unlikely(cons_tail.pos == prod_tail.pos))
        return false;

      new (x) T(std::move(ring[to_id(cons_tail.pos)]));
      ring[to_id(cons_tail.pos)].~T();

      ++cons_tail.pos;
    } else if (flags & ConQueueMode::F_MC_HTS_DEQ) {
      union po_val_t ot, nt;
      ot.raw = __atomic_load_n(&cons_head.raw, __ATOMIC_ACQUIRE);
      do {
        if (__glibc_unlikely(ot.pos == prod_tail.pos))
          return false;
        nt.pos = ot.pos + 1;
      } while (!__atomic_compare_exchange_n(&cons_head.raw, &ot.raw, nt.raw,
                                            true, __ATOMIC_ACQUIRE,
                                            __ATOMIC_ACQUIRE));
      new (x) T(std::move(ring[to_id(ot.pos)]));
      ring[to_id(ot.pos)].~T();
      hts_update_ht(&cons_tail, 1, ot.pos);
    } else {
      union po_val_t ot, nt;
      ot.raw = __atomic_load_n(&cons_head.raw, __ATOMIC_ACQUIRE);
      do {
        if (__glibc_unlikely(ot.pos == prod_tail.pos))
          return false;
        nt.pos = ot.pos + 1;
        nt.cnt = ot.cnt + 1;
      } while (!__atomic_compare_exchange_n(&cons_head.raw, &ot.raw, nt.raw,
                                            true, __ATOMIC_ACQUIRE,
                                            __ATOMIC_ACQUIRE));
      new (x) T(std::move(ring[to_id(ot.pos)]));
      ring[to_id(ot.pos)].~T();
      rts_update_ht(&cons_head, &cons_tail);
    }
    return true;
  }
  /**
   * @brief pop out several elements as much as possible
   *
   * @param first output first iterator
   * @param last output last iterator
   * @return The number of poped elements
   */
  template <typename Iter> uint32_t pop_bulk(Iter first, Iter last) {
    uint32_t l = 0;
    uint32_t count = std::distance(first, last);
    if (flags & ConQueueMode::F_SC_DEQ) {
      while (count != 0 && cons_tail.pos < prod_tail.pos) {
        new (&*(first++)) T(std::move(ring[to_id(cons_tail.pos + l)]));
        ring[to_id(cons_tail.pos + l)].~T();
        --count;
        ++l;
      }
      cons_tail.pos += l;
    } else if (flags & ConQueueMode::F_MC_HTS_DEQ) {
      union po_val_t ot, nt;
      ot.raw = __atomic_load_n(&cons_head.raw, __ATOMIC_ACQUIRE);
      do {
        l = std::min(count, prod_tail.pos - ot.pos);
        if (l == 0)
          return 0;
        nt.pos = ot.pos + l;
      } while (!__atomic_compare_exchange_n(&cons_head.raw, &ot.raw, nt.raw,
                                            true, __ATOMIC_ACQUIRE,
                                            __ATOMIC_ACQUIRE));
      for (uint32_t i = 0; i < l; ++i) {
        new (&*(first++)) T(std::move(ring[to_id(ot.pos + i)]));
        ring[to_id(ot.pos + i)].~T();
      }
      hts_update_ht(&cons_tail, l, ot.pos);
    } else {
      union po_val_t ot, nt;
      ot.raw = __atomic_load_n(&cons_head.raw, __ATOMIC_ACQUIRE);
      do {
        l = std::min(count, prod_tail.pos - ot.pos);
        if (l == 0)
          return 0;
        nt.pos = ot.pos + l;
        nt.cnt = ot.cnt + 1;
      } while (!__atomic_compare_exchange_n(&cons_head.raw, &ot.raw, nt.raw,
                                            true, __ATOMIC_ACQUIRE,
                                            __ATOMIC_ACQUIRE));
      for (uint32_t i = 0; i < l; ++i) {
        new (&*(first++)) T(std::move(ring[to_id(ot.pos + i)]));
        ring[to_id(ot.pos + i)].~T();
      }
      rts_update_ht(&cons_head, &cons_tail);
    }
    return l;
  }

private:
  union po_val_t {
    struct {
      uint32_t pos;
      uint32_t cnt;
    };
    uint64_t raw;
  };

  /**
   *   t   --->  h
   * [][][][][][][]
   */
  T *ring;
  bool self_alloc;
  uint32_t flags;
  /**
   * if F_EXACT_SZ: `count_mask` is actual size
   * else:          `count_mask` is minmax 2^n - 1
   */
  uint32_t count_mask;
  uint32_t max_size;

  __attribute__((aligned(64))) volatile po_val_t prod_head;
  volatile po_val_t prod_tail;
  __attribute__((aligned(64))) volatile po_val_t cons_head;
  volatile po_val_t cons_tail;

  static const int REP_COUNT = 3;

  uint32_t to_id(uint32_t i) {
    return (flags & ConQueueMode::F_EXACT_SZ) ? (i % count_mask)
                                              : (i & count_mask);
  }

  static uint32_t aligned_size(uint32_t size, uint32_t flags) {
    if ((flags & ConQueueMode::F_EXACT_SZ) == false) {
      --size;
      size |= size >> 1;
      size |= size >> 2;
      size |= size >> 4;
      size |= size >> 8;
      size |= size >> 16;
    }
    return size + 1;
  }

  static void hts_update_ht(volatile po_val_t *ht_, uint32_t n,
                            uint32_t oht_pos) {
    // Wait for other threads to finish copying
    int rep = 0;
    while (ht_->pos != oht_pos) {
      _mm_pause();
      if (++rep == REP_COUNT) {
        rep = 0;
        std::this_thread::yield();
      }
    }
    ht_->pos += n;
  }

  static void rts_update_ht(volatile po_val_t *ht, volatile po_val_t *ht_) {
    union po_val_t h, oh, nh;
    oh.raw = __atomic_load_n(&ht_->raw, __ATOMIC_ACQUIRE);
    while (1) {
      h.raw = __atomic_load_n(&ht->raw, __ATOMIC_RELAXED);
      nh.raw = oh.raw;
      if ((++nh.cnt) == h.cnt)
        nh.pos = h.pos;
      if (__atomic_compare_exchange_n(&ht_->raw, &oh.raw, nh.raw, true,
                                      __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
        break;
      std::this_thread::yield();
    }
  }
};

template <typename T> class CLQueue {
public:
  CLQueue(uint32_t max_size, uint32_t flags = ConQueueMode::F_MP_HTS_ENQ |
                                              ConQueueMode::F_MC_HTS_DEQ)
      : flags(flags) {
    if (max_size == 0)
      throw "max_size is 0";
    if ((this->flags & F_MP_HTS_ENQ) && (this->flags & F_MP_RTS_ENQ))
      throw "queue producer flag error";
    if ((this->flags & F_MC_RTS_DEQ) && (this->flags & F_MC_HTS_DEQ))
      throw "queue consumer flag error";
    if (this->flags & F_EXACT_SZ)
      throw "queue exacting size is not supported";

    // default flag set
    if (!(this->flags & (F_SP_ENQ | F_MP_RTS_ENQ | F_MP_HTS_ENQ)))
      this->flags |= F_MP_HTS_ENQ;
    if (!(this->flags & (F_SC_DEQ | F_MC_RTS_DEQ | F_MC_HTS_DEQ)))
      this->flags |= F_MC_HTS_DEQ;

    // find max_size = SLOT_MAX_INLINE * 2^n
    uint32_t s = SLOT_MAX_INLINE;
    while (s < max_size)
      s <<= 1;
    this->max_size = s;
    max_line = s / SLOT_MAX_INLINE;
    max_line_mask = max_line - 1;
    max_line_mask_bits = __builtin_popcount(max_line_mask);
    cls = (cache_line_t *)aligned_alloc(64, max_line * sizeof(cache_line_t));
    clear();
  }
  ~CLQueue() {
    clear();
    free(cls);
  }

  uint32_t capacity() const { return max_size; }
  uint32_t size() const {
    return head.load(std::memory_order_relaxed) -
           tail.load(std::memory_order_relaxed);
  }
  uint32_t empty() const {
    return head.load(std::memory_order_relaxed) ==
           tail.load(std::memory_order_relaxed);
  }
  uint32_t full() const {
    return head.load(std::memory_order_relaxed) -
               tail.load(std::memory_order_relaxed) ==
           capacity();
  }
  // Guarantee that the reference will not be poped
  T &top() const {
    uint32_t ot = tail.load(std::memory_order_relaxed);
    uint32_t li, si;
    cache_line_t *cl;
    li = ot & max_line_mask;
    cl = &cls[li];
    si = (ot >> max_line_mask_bits) % SLOT_MAX_INLINE;
    return cl->slots[si];
  }
  void clear() {
    for (uint32_t i = tail; i != head; ++i) {
      uint32_t li = i & max_line_mask;
      cache_line_t *cl = &cls[i];
      uint32_t si = (i >> max_line_mask_bits) % SLOT_MAX_INLINE;
      cl->slots[si].~T();
    }
    head = tail = 0;
    for (uint32_t li = 0; li < max_line; ++li) {
      cls[li].hf1 = cls[li].tf1 = 0;
      cls[li].hf2 = cls[li].tf2 = 1;
    }
    cls[0].hf1 = cls[0].tf1 = 1;
  }
  bool push(const T &x) { return emplace(std::forward<const T &>(x)); }
  bool push(T &&x) { return emplace(std::forward<T>(x)); }
  template <typename... Args> bool emplace(Args &&...args) {
    uint32_t li, next_li, si;
    cache_line_t *cl;
    if (flags & F_SP_ENQ) {
      uint32_t oh = head.load(std::memory_order_relaxed);
      li = oh & max_line_mask;
      cl = &cls[li];
      if (__glibc_unlikely(cl->hf2 - cl->tf2 == SLOT_MAX_INLINE))
        return false;
      head.fetch_add(1, std::memory_order_relaxed);
      si = (oh >> max_line_mask_bits) % SLOT_MAX_INLINE;
      new (&cl->slots[si]) T(std::forward<Args>(args)...);
    } else {
      uint32_t oh = head.load(std::memory_order_acquire);
      do {
        li = oh & max_line_mask;
        cl = &cls[li];
        if (__glibc_unlikely(cl->hf2 - cl->tf2 == SLOT_MAX_INLINE &&
                             head.load(std::memory_order_acquire) == oh))
          return false;
      } while (
          !head.compare_exchange_weak(oh, oh + 1, std::memory_order_acquire));

      si = (oh >> max_line_mask_bits) % SLOT_MAX_INLINE;
      new (&cl->slots[si]) T(std::forward<Args>(args)...);

      if (flags & F_MP_HTS_ENQ) {
        int rep = 0;
        while (cl->hf1 != cl->hf2) {
          _mm_pause();
          if (++rep == REP_COUNT) {
            rep = 0;
            std::this_thread::yield();
          }
        }
        next_li = (oh + 1) & max_line_mask;
        ++cls[next_li].hf1;
      }
    }

    ++cl->hf2;
    return true;
  }
  bool pop(T *x) {
    uint32_t li, next_li, si;
    cache_line_t *cl;
    if (flags & F_SC_DEQ) {
      uint32_t ot = tail.load(std::memory_order_relaxed);
      li = ot & max_line_mask;
      cl = &cls[li];
      if (__glibc_unlikely(cl->hf2 == cl->tf2))
        return false;
      tail.fetch_add(1, std::memory_order_relaxed);
      si = (ot >> max_line_mask_bits) % SLOT_MAX_INLINE;
      new (x) T(std::move(cl->slots[si]));
      cl->slots[si].~T();
    } else {
      uint32_t ot = tail.load(std::memory_order_acquire);
      do {
        li = ot & max_line_mask;
        cl = &cls[li];
        if (__glibc_unlikely(cl->hf2 == cl->tf2 &&
                             tail.load(std::memory_order_acquire) == ot))
          return false;
      } while (
          !tail.compare_exchange_weak(ot, ot + 1, std::memory_order_acquire));

      si = (ot >> max_line_mask_bits) % SLOT_MAX_INLINE;
      new (x) T(std::move(cl->slots[si]));
      cl->slots[si].~T();

      if (flags & F_MC_HTS_DEQ) {
        int rep = 0;
        while (cl->tf1 != cl->tf2) {
          _mm_pause();
          if (++rep == REP_COUNT) {
            rep = 0;
            std::this_thread::yield();
          }
        }
        next_li = (ot + 1) & max_line_mask;
        ++cls[next_li].tf1;
      }
    }

    ++cl->tf2;
    return true;
  }

private:
  static constexpr size_t T_SIZE = sizeof(T);
  static constexpr size_t CACHE_LINE_SIZE = 64;

  static_assert(T_SIZE == 1 || T_SIZE == 2 || T_SIZE == 4 || T_SIZE == 8,
                "CLQueue type size must be 1, 2, 4 or 8.");

  static const int SLOT_MAX_INLINE = (CACHE_LINE_SIZE - 8) / T_SIZE;
  static const int REP_COUNT = 3;

  /*    cache line size
   * [ f  |    slots    ]
   * [ f  |    slots    ]
   * [ f  |    slots    ]
   *
   * push/pop order: Stroke order of 'W'
   */
  struct cache_line_t {
    volatile uint16_t hf1, hf2, tf1, tf2;
    T slots[SLOT_MAX_INLINE];
  };

  uint32_t flags;
  cache_line_t *cls;
  uint32_t max_line;
  uint32_t max_size;
  uint32_t max_line_mask;
  uint8_t max_line_mask_bits;

  __attribute__((aligned(64))) std::atomic<uint32_t> head;
  __attribute__((aligned(64))) std::atomic<uint32_t> tail;
};

#include "extend_mutex.h"

template <typename T> class MpScNoRestrictBoundedQueue {
public:
  MpScNoRestrictBoundedQueue(uint32_t init_size = 65536,
                             uint32_t flags = ConQueueMode::F_MP_RTS_ENQ |
                                              ConQueueMode::F_SC_DEQ)
      : cons(0), flags(flags) {
    if (init_size == 0)
      throw "init size is 0";
    if (this->flags & F_MP_HTS_ENQ)
      throw "queue producer flag error";
    if ((this->flags & F_SP_ENQ) && (this->flags & F_MP_RTS_ENQ))
      throw "queue producer flag error";
    if (this->flags & (F_MC_RTS_DEQ | F_MC_HTS_DEQ))
      throw "queue consumer flag error";
    if (this->flags & F_EXACT_SZ)
      throw "queue exact size flag error";

    // default flag set
    if (!(this->flags & (F_SP_ENQ | F_MP_RTS_ENQ | F_MP_HTS_ENQ)))
      this->flags |= F_MP_HTS_ENQ;
    if (!(this->flags & F_SC_DEQ))
      this->flags |= F_MC_HTS_DEQ;

    if (check_little_endian()) {
      w_.li.max_size = init_size;
      w_.li.arr = new T[init_size];
    } else {
      w_.bi.max_size = init_size;
      w_.bi.arr = new T[init_size];
    }
    prod_head.raw = prod_tail.raw = 0;
  }
  ~MpScNoRestrictBoundedQueue() {
    clear();
    if (check_little_endian()) {
      delete[] w_.li.arr;
    } else {
      delete[] w_.bi.arr;
    }
  }

  uint32_t capacity() const { return get_max_size(); }
  uint32_t size() const { return prod_tail.pos - cons; }
  bool empty() const { return prod_tail.pos == cons; }
  bool full() const { return false; }
  // Guarantee that the reference will not be poped
  T &top() const {
    auto p = get_word_pair();
    return p.first[prod_tail.pos % p.second];
  }
  void clear() {
    auto p = get_word_pair();
    uint32_t t = prod_tail.pos;
    for (uint32_t j = cons; j != t; ++j) {
      p.first[j % p.second].~T();
    }
    prod_tail.pos = prod_head.pos = cons = 0;
  }

  // No sequentially consistency, i.e. the elements of the push may not be
  // visible when the thread is popped. This is a better solution for
  // high performance.
  bool push(const T &x) { return emplace(std::forward<const T &>(x)); }
  bool push(T &&x) { return emplace(std::forward<T>(x)); }
  template <typename... Args> bool emplace(Args &&...args) {
    po_val_t oh;
    if (flags & ConQueueMode::F_SP_ENQ) {
      uint32_t i = prod_head.pos;
      if (__glibc_unlikely(i - cons >= get_max_size()))
        expand_arr(i);

      auto p = get_word_pair();
      new (&p.first[i & (p.second - 1)]) T(std::forward<Args>(args)...);
      ++prod_head.pos;
      ++prod_tail.pos;
    } else {
      oh.raw = __atomic_fetch_add(&prod_head.raw, (1ul << 32) | 1ul,
                                  __ATOMIC_ACQUIRE);
      uint32_t i = oh.pos;

    retry:
      expand_lck_.lock_shared();
      if (__glibc_unlikely(i - cons >= get_max_size())) {
        if (!expand_lck_.try_lock_prompt()) {
          expand_lck_.unlock_shared();
          goto retry;
        }
        if (i - cons >= get_max_size())
          expand_arr(i);
        expand_lck_.unlock();
        goto retry;
      }

      auto p = get_word_pair();
      new (&p.first[i & (p.second - 1)]) T(std::forward<Args>(args)...);

      // use rts mode to remove dead lock
      rts_update_ht(&prod_head, &prod_tail);
      expand_lck_.unlock_shared();
    }

    return true;
  }

  // single-thread pop.
  // ensure the queue is not empty before call it.
  bool pop(T *x) {
    // need thread-fence, otherwise `arr` and `max_size` is inconsistent.
    auto p = get_word_pair();
    *x = std::move(p.first[cons & (p.second - 1)]);
    ++cons;
    return true;
  }

private:
  union po_val_t {
    struct {
      uint32_t pos;
      uint32_t cnt;
    };
    uint64_t raw;
  };

  union word {
    union {
      struct {
        T *arr;
        uint32_t max_size;
        uint32_t __padding__;
      };
      uint32_t raw[4];
    } bi;
    union {
      struct {
        uint32_t __padding__;
        uint32_t max_size;
        T *arr;
      };
      uint32_t raw[4];
    } li;
  };

  T *get_arr() const volatile {
    if (check_little_endian()) {
      return w_.li.arr;
    } else {
      return w_.bi.arr;
    }
  }

  uint32_t get_max_size() const volatile {
    if (check_little_endian()) {
      return w_.li.max_size;
    } else {
      return w_.bi.max_size;
    }
  }

  // load `arr` and `max_size` together
  std::pair<T *, uint32_t> get_word_pair() const {
    if (check_little_endian()) {
      return {w_.li.arr, w_.li.max_size};
    } else {
      return {w_.bi.arr, w_.bi.max_size};
    }
  }

  void expand_arr(uint32_t current_i) {
    auto p = get_word_pair();
    uint32_t old_size = p.second;
    T *old_arr = p.first;

    uint32_t new_size = get_max_size() << 1;
    T *new_arr = new T[new_size];

    // The other threads with `oh.pos >= i` are not assigning element to the
    // queue at this time. And other threads with `oh.pos < i` have assigned
    // element to the queue.

    for (uint32_t j = current_i - 1; j != cons - 1; --j) {
      new_arr[j % new_size] = old_arr[j % old_size];
    }
    update_arr_and_size(new_arr, new_size);
    delete[] old_arr;
  }

  // In order to achieve atomic update of `arr` pointer and `max_size`,
  // it is necessary to determine the system endian node and update both in 64
  // bits if possible, because `__atomic_store_16` is slower than
  // `__atomic_store_n`.
  void update_arr_and_size(T *new_arr, uint32_t new_size) {
    if (check_little_endian()) {
      if ((((uint64_t)new_arr) >> 32) == (((uint64_t)get_arr()) >> 32)) {
        __atomic_store_n((uint64_t *)(&w_.li.raw[1]),
                         (((uint64_t)new_arr) << 32) | new_size,
                         __ATOMIC_RELAXED);
      } else {
        word nw;
        nw.li.arr = new_arr;
        nw.li.max_size = new_size;
        __atomic_store(&w_, &nw, __ATOMIC_RELAXED);
      }
    } else {
      // TODO ...
      word nw;
      nw.bi.arr = new_arr;
      nw.bi.max_size = new_size;
      __atomic_store(&w_, &nw, __ATOMIC_RELAXED);
    }
  }

  static bool check_little_endian() {
    union judge_u {
      int a;
      char b;
    };
    judge_u u = {};
    u.a = 0x12345678;
    return 0x78 == u.b;
  }

  static void rts_update_ht(volatile po_val_t *ht, volatile po_val_t *ht_) {
    po_val_t h, oh, nh;
    oh.raw = __atomic_load_n(&ht_->raw, __ATOMIC_ACQUIRE);
    while (1) {
      h.raw = __atomic_load_n(&ht->raw, __ATOMIC_RELAXED);
      nh.raw = oh.raw;
      if ((++nh.cnt) == h.cnt)
        nh.pos = h.pos;
      if (__atomic_compare_exchange_n(&ht_->raw, &oh.raw, nh.raw, true,
                                      __ATOMIC_RELEASE, __ATOMIC_ACQUIRE))
        break;
      std::this_thread::yield();
    }
  }

  uint32_t flags;
  __attribute__((aligned(64))) volatile word w_;
  intention_mutex expand_lck_;
  volatile po_val_t prod_head, prod_tail;
  __attribute__((aligned(64))) volatile uint32_t cons;
};

#endif // __CONQUEUE_H__