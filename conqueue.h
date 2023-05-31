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
#include <thread>

enum ConQueueMode {
  F_SP_ENQ = 1,
  F_SC_DEQ = 2,
  F_EXACT_SZ = 4,
  F_MP_RTS_ENQ = 8,
  F_MC_RTS_DEQ = 16,
  F_MP_ENQ = 32,
  F_MC_DEQ = 64,
};
template <typename T, typename Alloc = std::allocator<T>> class ConQueue {
public:
  ConQueue(T *ring, uint32_t max_size,
           uint32_t flags = ConQueueMode::F_MP_ENQ | ConQueueMode::F_MC_DEQ)
      : ring(ring), self_alloc(false), flags(flags), max_size(max_size) {
    prod_head.raw = prod_tail.raw = cons_head.raw = cons_tail.raw = 0;

    if (ring == nullptr)
      throw "ring array is null";
    if (max_size == 0)
      throw "max_size is 0";
    if ((this->flags & F_SP_ENQ) && (this->flags & (F_MP_RTS_ENQ | F_MP_ENQ)))
      throw "queue producer flag error";
    if ((this->flags & F_MP_RTS_ENQ) && (this->flags & F_MP_ENQ))
      throw "queue producer flag error";
    if ((this->flags & F_SC_DEQ) && (this->flags & (F_MC_RTS_DEQ | F_MC_DEQ)))
      throw "queue consumer flag error";
    if ((this->flags & F_MC_RTS_DEQ) && (this->flags & F_MC_DEQ))
      throw "queue consumer flag error";

    // default flag set
    if (!(this->flags & (F_SP_ENQ | F_MP_RTS_ENQ | F_MP_ENQ)))
      this->flags |= F_MP_ENQ;
    if (!(this->flags & (F_SC_DEQ | F_MC_RTS_DEQ | F_MC_DEQ)))
      this->flags |= F_MC_DEQ;

    count_mask = aligned_size(max_size, this->flags) - 1;
  }
  ConQueue(uint32_t max_size,
           uint32_t flags = ConQueueMode::F_MP_ENQ | ConQueueMode::F_MC_DEQ)
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
    } else if (flags & ConQueueMode::F_MP_ENQ) {
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
      default_update_ht(&prod_tail, 1, oh.pos);
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
    } else if (flags & ConQueueMode::F_MP_ENQ) {
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
      default_update_ht(&prod_tail, l, oh.pos);
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
    } else if (flags & ConQueueMode::F_MC_DEQ) {
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
      default_update_ht(&cons_tail, 1, ot.pos);
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
    } else if (flags & ConQueueMode::F_MC_DEQ) {
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
      default_update_ht(&cons_tail, l, ot.pos);
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

  static void default_update_ht(volatile po_val_t *ht_, uint32_t n,
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

template <typename T, bool USE_BLOCK = false> class CLQueue {
public:
  CLQueue(uint32_t max_size,
          uint32_t flags = ConQueueMode::F_MP_ENQ | ConQueueMode::F_MC_DEQ)
      : flags(flags) {
    if (max_size == 0)
      throw "max_size is 0";
    if ((this->flags & F_MP_ENQ) && (this->flags & F_MP_RTS_ENQ))
      throw "queue producer flag error";
    if ((this->flags & F_MC_RTS_DEQ) && (this->flags & F_MC_DEQ))
      throw "queue consumer flag error";
    if (this->flags & F_EXACT_SZ)
      throw "queue exacting size is not supported";

    // default flag set
    if (!(this->flags & (F_SP_ENQ | F_MP_RTS_ENQ | F_MP_ENQ)))
      this->flags |= F_MP_ENQ;
    if (!(this->flags & (F_SC_DEQ | F_MC_RTS_DEQ | F_MC_DEQ)))
      this->flags |= F_MC_DEQ;

    // find max_size = SLOT_MAX_INLINE * 2^n
    uint32_t s = SLOT_MAX_INLINE;
    while (s < max_size)
      s <<= 1;
    this->max_size = s;
    max_line = s / SLOT_MAX_INLINE;
    max_line_mask = max_line - 1;
    max_line_mask_bits = __builtin_popcount(max_line_mask);
    cls = (cache_line_t *)aligned_alloc(4096, max_line * sizeof(cache_line_t));
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
    li = get_li(ot);
    cl = &cls[li];
    si = get_si(ot);
    return cl->slots[si];
  }

  void clear() {
    for (uint32_t i = tail; i != head; ++i) {
      uint32_t li = get_li(i);
      cache_line_t *cl = &cls[li];
      uint32_t si = get_si(i);
      cl->slots[si].~T();
    }
    head = tail = 0;
    for (uint32_t li = 0; li < max_line; ++li) {
      cls[li].hf1 = cls[li].tf1 = 0;
      cls[li].hf2 = cls[li].tf2 = 1;
    }
    cls[0].hf1 = cls[0].tf1 = 1;
  }

  bool push(const T &x) {
    return emplace_impl<false>(std::forward<const T &>(x));
  }
  bool push(T &&x) { return emplace_impl<false>(std::forward<T>(x)); }

  /**
   * Push an element for better performance without checking queue is full.
   * @warning If queue is full, the thread will occupy a slot as usual, which
   * may causes pop operation stall. So in this case, the call will throw an
   * error.
   */
  bool push_unsafe(const T &x) {
    return emplace_impl<true>(std::forward<const T &>(x));
  }
  bool push_unsafe(T &&x) { return emplace_impl<true>(std::forward<T>(x)); }

  template <typename... Args> bool emplace(Args &&...args) {
    return emplace_impl<false>(std::forward<Args>(args)...);
  }
  template <typename... Args> bool emplace_unsafe(Args &&...args) {
    return emplace_impl<true>(std::forward<Args>(args)...);
  }

private:
  template <bool TF, typename... Args> bool emplace_impl(Args &&...args) {
    uint32_t oh, li, next_li, si;
    cache_line_t *cl;
    if (flags & F_SP_ENQ) {
      oh = head.load(std::memory_order_relaxed);
      li = get_li(oh);
      cl = &cls[li];
      if (__glibc_unlikely(cl->hf2 - cl->tf2 == SLOT_MAX_INLINE))
        return false;
      head.fetch_add(1, std::memory_order_relaxed);
      si = get_si(oh);
      new (&cl->slots[si]) T(std::forward<Args>(args)...);
    } else {
      if (TF) {
        oh = head.fetch_add(1, std::memory_order_relaxed);
        li = get_li(oh);
        cl = &cls[li];
        if (__glibc_unlikely(cl->hf2 - cl->tf2 == SLOT_MAX_INLINE))
          throw "queue full";
      } else {
        oh = head.load(std::memory_order_acquire);
        do {
          li = get_li(oh);
          cl = &cls[li];
          if (__glibc_unlikely(cl->hf2 - cl->tf2 == SLOT_MAX_INLINE))
            return false;
        } while (
            !head.compare_exchange_weak(oh, oh + 1, std::memory_order_acquire));
      }

      si = get_si(oh);
      new (&cl->slots[si]) T(std::forward<Args>(args)...);

      if (flags & F_MP_ENQ) {
        int rep = 0;
        while (cl->hf1 != cl->hf2) {
          _mm_pause();
          if (++rep == REP_COUNT) {
            rep = 0;
            std::this_thread::yield();
          }
        }
        next_li = get_li(oh + 1);
        ++cls[next_li].hf1;
      }
    }

    // ! If `flag` has `F_MP_RTS_ENQ`, maybe cause write loss error (operator++)
    // when some producers get same cache line. So this queue need a lot of
    // cache lines to avoid this problem.
    ++cl->hf2;
    return true;
  }

public:
  bool pop(T *x) {
    uint32_t ot, li, next_li, si;
    cache_line_t *cl;
    if (flags & F_SC_DEQ) {
      ot = tail.load(std::memory_order_relaxed);
      li = get_li(ot);
      cl = &cls[li];
      if (__glibc_unlikely(cl->hf2 == cl->tf2))
        return false;
      tail.fetch_add(1, std::memory_order_acquire);
      si = get_si(ot);
      *x = std::move(cl->slots[si]);
      cl->slots[si].~T();
    } else {
      /**
       * Needed to determine if the queue is empty before the queue pop element.
       *
       * `dequeue_overcommit` indicates the number of queues currently being
       * popped. `overcommit` indicates the number of failed queue pops.
       *
       * The distance between `dequeue_overcommit` and `overcommit` represents
       * the position of the slot that needs to be inserted by the threads
       * currently being popped. If the `hf2` and `tf2` of the slot are not
       * equal, it means that the queue has not yet inserted an element here,
       * the function returns false and add `overcommit` value so that the slot
       * position is moved forward on the next pop call.
       */
      uint32_t doc = dequeue_overcommit.fetch_add(1, std::memory_order_relaxed);
      ot = doc - overcommit.load(std::memory_order_relaxed);
      li = get_li(ot);
      cl = &cls[li];
      if (__glibc_likely(cl->hf2 != cl->tf2)) {
        ot = tail.fetch_add(1, std::memory_order_relaxed);
        li = get_li(ot);
        cl = &cls[li];
        si = get_si(ot);
        /**
         * Running here means that the slot has been occupied by the push
         * thread, so we need to wait for it to insert the element.
         */
        int rep = 0;
        while (__glibc_unlikely(cl->hf2 == cl->tf2)) {
          _mm_pause();
          if (++rep == REP_COUNT) {
            rep = 0;
            std::this_thread::yield();
          }
        }
      } else {
        overcommit.fetch_add(1, std::memory_order_release);
        return false;
      }

      *x = std::move(cl->slots[si]);
      cl->slots[si].~T();

      if (flags & F_MC_DEQ) {
        int rep = 0;
        while (cl->tf1 != cl->tf2) {
          _mm_pause();
          if (++rep == REP_COUNT) {
            rep = 0;
            std::this_thread::yield();
          }
        }
        next_li = get_li(ot + 1);
        ++cls[next_li].tf1;
      }
    }

    // ! If `flag` has `F_MC_RTS_DEQ`, maybe cause write loss error (operator++)
    // when some consumers get same cache line. So this queue need a lot of
    // cache lines to avoid this problem.
    ++cl->tf2;
    return true;
  }

private:
  static constexpr size_t T_SIZE = sizeof(T);
  static constexpr size_t CACHE_LINE_SIZE = 64;
  static constexpr int T_INLINE_MIN = 7;
  static constexpr size_t CACHE_LINE_HEADER_SIZE = 8;

  static constexpr size_t LINE_SIZE =
      ((T_INLINE_MIN * T_SIZE + CACHE_LINE_HEADER_SIZE) % CACHE_LINE_SIZE)
          ? (((T_INLINE_MIN * T_SIZE + CACHE_LINE_HEADER_SIZE) /
              CACHE_LINE_SIZE * CACHE_LINE_SIZE) +
             CACHE_LINE_SIZE)
          : (T_INLINE_MIN * T_SIZE + CACHE_LINE_HEADER_SIZE);
  static constexpr int SLOT_MAX_INLINE =
      (LINE_SIZE - CACHE_LINE_HEADER_SIZE) / T_SIZE;
  static constexpr int REP_COUNT = 3;
  static constexpr int BLOCK_WIDTH = CACHE_LINE_SIZE;

  /*      cache line
   * [ f  |    slots    ]
   * [ f  |    slots    ]
   * [ f  |    slots    ]
   *
   * push/pop order: Stroke order of 'W'
   */
  struct cache_line_t {
    volatile uint16_t hf1, tf1, hf2, tf2;
    T slots[SLOT_MAX_INLINE];
  } __attribute__((aligned(CACHE_LINE_SIZE)));

  uint32_t flags;
  cache_line_t *cls;
  uint32_t max_line;
  uint32_t max_size;
  uint32_t max_line_mask;
  uint8_t max_line_mask_bits;

  __attribute__((aligned(64))) std::atomic<uint32_t> dequeue_overcommit = {0};
  std::atomic<uint32_t> overcommit = {0};
  __attribute__((aligned(64))) std::atomic<uint32_t> head;
  __attribute__((aligned(64))) std::atomic<uint32_t> tail;

  uint32_t get_li(uint32_t n) const {
    if (USE_BLOCK)
      return ((n % BLOCK_WIDTH) +
              n / (SLOT_MAX_INLINE * BLOCK_WIDTH) * BLOCK_WIDTH) &
             max_line_mask;
    else
      return n & max_line_mask;
  }

  uint32_t get_si(uint32_t n) const {
    if (USE_BLOCK)
      return (n % (SLOT_MAX_INLINE * BLOCK_WIDTH)) / BLOCK_WIDTH;
    else
      return (n >> max_line_mask_bits) % SLOT_MAX_INLINE;
  }
};

#endif // __CONQUEUE_H__