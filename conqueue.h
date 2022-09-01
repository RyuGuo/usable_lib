/**
 * @file conqueue.h
 * @brief lock-free concurrent queue
 * @version 0.1
 * @date 2022-06-12
 */

#ifndef __CONQUEUE_H__
#define __CONQUEUE_H__

#include "pl_allocator.h"
#include <atomic>
#include <thread>
#include <vector>

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
      : ring(ring), flags(flags), _max_size(max_size), self_alloc(false) {
    prod_head.raw = prod_tail.raw = cons_head.raw = cons_tail.raw = 0;

    if (ring == nullptr)
      throw "ring array is null";
    if (max_size == 0)
      throw "max_size is 0";
    if ((flags & F_SP_ENQ) && (flags & (F_MP_RTS_ENQ | F_MP_HTS_ENQ)))
      throw "queue producer flag error";
    if ((flags & F_MP_RTS_ENQ) && (flags & F_MP_HTS_ENQ))
      throw "queue producer flag error";
    if ((flags & F_SC_DEQ) && (flags & (F_MC_RTS_DEQ | F_MC_HTS_DEQ)))
      throw "queue consumer flag error";
    if ((flags & F_MC_RTS_DEQ) && (flags & F_MC_HTS_DEQ))
      throw "queue consumer flag error";

    // default flag set
    if (!(flags & (F_SP_ENQ | F_MP_RTS_ENQ | F_MP_HTS_ENQ)))
      flags |= F_MP_HTS_ENQ;
    if (!(flags & (F_SC_DEQ | F_MC_RTS_DEQ | F_MC_HTS_DEQ)))
      flags |= F_MC_HTS_DEQ;

    count_mask = aligned_size(max_size, flags) - 1;
  }
  ConQueue(uint32_t max_size, uint32_t flags = ConQueueMode::F_MP_HTS_ENQ |
                                               ConQueueMode::F_MC_HTS_DEQ)
      : ConQueue(Alloc().allocate(aligned_size(max_size, flags)), max_size,
                 flags) {
    self_alloc = true;
  }
  ~ConQueue() {
    uint32_t _h = prod_head.pos, _t = cons_head.pos;
    for (uint32_t i = _t; i != _h; ++i) {
      ring[to_id(i)].~T();
    }
    if (self_alloc)
      Alloc().deallocate(ring, _max_size);
  }
  uint32_t capacity() { return _max_size; }
  uint32_t size() { return prod_tail.pos - cons_tail.pos; }
  bool empty() { return prod_tail.pos == cons_tail.pos; }
  bool full() { return prod_tail.pos - cons_tail.pos == capacity(); }
  // Guarantee that the reference will not be poped
  T &top() { return ring[cons_tail.pos]; }
  void clear() { pop_bulk(nullptr, size()); }
  bool push(const T &__x) {
    if (flags & ConQueueMode::F_SP_ENQ) {
      if (prod_head.pos - cons_tail.pos == capacity())
        return false;

      new (&ring[to_id(prod_head.pos)]) T(__x);

      ++prod_head.pos;
      ++prod_tail.pos;
    } else {
      union po_val_t oh, nh;
      oh.raw = __atomic_load_n(&prod_head.raw, __ATOMIC_ACQUIRE);
      do {
        if (oh.pos - cons_tail.pos == capacity())
          return false;
        nh.pos = oh.pos + 1;
        if (flags & ConQueueMode::F_MP_RTS_ENQ)
          nh.cnt = oh.cnt + 1;
      } while (!__atomic_compare_exchange_n(&prod_head.raw, &oh.raw, nh.raw,
                                            true, __ATOMIC_ACQUIRE,
                                            __ATOMIC_ACQUIRE));

      new (&ring[to_id(oh.pos)]) T(__x);

      if (flags & ConQueueMode::F_MP_HTS_ENQ) {
        hts_update_ht(&prod_tail, 1, oh.pos);
      } else {
        rts_update_ht(&prod_head, &prod_tail);
      }
    }
    return true;
  }
  template <typename... Args> bool emplace(Args &&...args) {
    if (flags & ConQueueMode::F_SP_ENQ) {
      if (prod_head.pos - cons_tail.pos == capacity())
        return false;

      new (&ring[to_id(prod_head.pos)]) T(std::forward<Args>(args)...);

      ++prod_head.pos;
      ++prod_tail.pos;
    } else {
      union po_val_t oh, nh;
      oh.raw = __atomic_load_n(&prod_head.raw, __ATOMIC_ACQUIRE);
      do {
        if (oh.pos - cons_tail.pos == capacity())
          return false;
        nh.pos = oh.pos + 1;
        if (flags & ConQueueMode::F_MP_RTS_ENQ)
          nh.cnt = oh.cnt + 1;
      } while (!__atomic_compare_exchange_n(&prod_head.raw, &oh.raw, nh.raw,
                                            true, __ATOMIC_ACQUIRE,
                                            __ATOMIC_ACQUIRE));

      new (&ring[to_id(oh.pos)]) T(std::forward<Args>(args)...);

      if (flags & ConQueueMode::F_MP_HTS_ENQ) {
        hts_update_ht(&prod_tail, 1, oh.pos);
      } else {
        rts_update_ht(&prod_head, &prod_tail);
      }
    }
    return true;
  }
  /**
   * @brief push several elements as much as possible
   *
   * @param v
   * @param size
   * @return The number of pushed elements
   */
  uint32_t push_bulk(const T *v, uint32_t count) {
    uint32_t l;
    if (flags & ConQueueMode::F_SP_ENQ) {
      l = std::min(capacity() - prod_head.pos + cons_tail.pos, count);
      for (uint32_t i = 0; i < l; ++i) {
        new (&ring[to_id(prod_head.pos + i)]) T(v[i]);
      }
      prod_head.pos += l;
      prod_tail.pos += l;
    } else {
      union po_val_t oh, nh;
      oh.raw = __atomic_load_n(&prod_head.raw, __ATOMIC_ACQUIRE);
      do {
        l = std::min(capacity() - oh.pos + cons_tail.pos, count);
        if (l == 0)
          return 0;
        nh.pos = oh.pos + l;
        if (flags & ConQueueMode::F_MP_RTS_ENQ)
          nh.cnt = oh.cnt + 1;
      } while (!__atomic_compare_exchange_n(&prod_head.raw, &oh.raw, nh.raw,
                                            true, __ATOMIC_ACQUIRE,
                                            __ATOMIC_ACQUIRE));

      for (uint32_t i = 0; i < l; ++i) {
        new (&ring[to_id(oh.pos + i)]) T(v[i]);
      }

      if (flags & ConQueueMode::F_MP_HTS_ENQ) {
        hts_update_ht(&prod_tail, l, oh.pos);
      } else {
        rts_update_ht(&prod_head, &prod_tail);
      }
    }
    return l;
  }
  /**
   * @brief push several elements as much as possible
   *
   * @param v
   * @return The vector of pushed elements
   */
  uint32_t push_bulk(const std::vector<T> &v) {
    return push_bulk(v.data(), v.size());
  }
  bool pop(T *__x) {
    if (flags & ConQueueMode::F_SC_DEQ) {
      if (cons_head.pos == prod_tail.pos)
        return false;

      new (__x) T(std::move(ring[to_id(cons_head.pos)]));
      ring[to_id(cons_head.pos)].~T();

      ++cons_head.pos;
      ++cons_tail.pos;
    } else {
      union po_val_t ot, nt;
      ot.raw = __atomic_load_n(&cons_head.raw, __ATOMIC_ACQUIRE);
      do {
        if (ot.pos == prod_tail.pos)
          return false;
        nt.pos = ot.pos + 1;
        if (flags & ConQueueMode::F_MC_RTS_DEQ)
          nt.cnt = ot.cnt + 1;
      } while (!__atomic_compare_exchange_n(&cons_head.raw, &ot.raw, nt.raw,
                                            true, __ATOMIC_ACQUIRE,
                                            __ATOMIC_ACQUIRE));

      new (__x) T(std::move(ring[to_id(ot.pos)]));
      ring[to_id(ot.pos)].~T();

      if (flags & ConQueueMode::F_MC_HTS_DEQ) {
        hts_update_ht(&cons_tail, 1, ot.pos);
      } else {
        rts_update_ht(&cons_head, &cons_tail);
      }
    }
    return true;
  }
  /**
   * @brief pop out several elements as much as possible
   *
   * @param v
   * @param count
   * @return The number of poped elements
   */
  uint32_t pop_bulk(T *v, uint32_t count) {
    uint32_t l = 0;
    if (flags & ConQueueMode::F_SC_DEQ) {
      uint32_t _count = count;
      while (count != 0 && cons_head.pos < prod_tail.pos) {
        new (v + l) T(std::move(ring[to_id(cons_head.pos + l)]));
        ring[to_id(cons_head.pos + l)].~T();
        --count;
        ++l;
      }
      cons_head += l;
      cons_tail += l;
    } else {
      union po_val_t ot, nt;
      ot.raw = __atomic_load_n(&cons_head.raw, __ATOMIC_ACQUIRE);
      do {
        l = std::min(count, prod_tail.pos - ot.pos);
        if (l == 0)
          return 0;
        nt.pos = ot.pos + l;
        if (flags & ConQueueMode::F_MC_RTS_DEQ)
          nt.cnt = ot.cnt + 1;
      } while (!__atomic_compare_exchange_n(&cons_head.raw, &ot.raw, nt.raw,
                                            true, __ATOMIC_ACQUIRE,
                                            __ATOMIC_ACQUIRE));

      for (uint32_t i = 0; i < l; ++i) {
        new (v + i) T(std::move(ring[to_id(ot.pos + i)]));
        ring[to_id(ot.pos + i)].~T();
      }

      if (flags & ConQueueMode::F_MC_RTS_DEQ) {
        hts_update_ht(&prod_tail, l, ot.pos);
      } else {
        rts_update_head(&prod_head, &prod_tail);
      }
    }
    return l;
  }
  /**
   * @brief pop out several elements as much as possible
   *
   * @param count
   * @return The vector of poped elements
   */
  std::vector<T> pop_bulk(uint32_t count) {
    std::vector<T> v;
    uint32_t l = 0;
    v.reserve(count);
    l = pop_bulk(v.data(), count);
    v.assign(v.data(), v.data() + l);
    return v;
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
  volatile po_val_t prod_head, prod_tail;
  volatile po_val_t cons_head, cons_tail;
  uint32_t flags;
  /**
   * if F_EXACT_SZ: `count_mask` is actual size
   * else:          `count_mask` is minmax 2^n - 1
   */
  uint32_t count_mask;
  uint32_t _max_size;

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
    while (__atomic_load_n(&ht_->pos, __ATOMIC_ACQUIRE) != oht_pos)
      std::this_thread::yield();
    __atomic_fetch_add(&ht_->pos, n, __ATOMIC_RELEASE);
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

template <typename T, template <typename _T> class Alloc = std::allocator>
class ConQueueEX {
  struct node {
    volatile node *next;
    T item;
  };
  struct copyable_pair {
    node *first;
    uint64_t second;
  };

public:
  ConQueueEX(uint32_t flags = ConQueueMode::F_MP_HTS_ENQ |
                              ConQueueMode::F_MC_HTS_DEQ)
      : flags(flags), __size(0), head(copyable_pair{nullptr, 0}) {
    node *dummy = Alloc<node>().allocate(1);
    dummy->next = nullptr;
    head = copyable_pair{dummy, 0};
    tail = dummy;
  }
  ~ConQueueEX() { clear(); }

  bool empty() { return __size == 0; }
  uint32_t size() { return __size; }
  // Guarantee that the reference will not be poped
  T &front() { return head.load().frist->next->item; }
  void clear() { pop_out_bulk(nullptr, size()); }
  bool push(const T &__x) {
    node *n = Alloc<node>().allocate(1);
    new (&n->item) T(__x);
    n->next = nullptr;
    node *taild = tail.exchange(n);
    taild->next = n;
    ++__size;
    return true;
  }
  template <typename... Args> bool emplace(Args &&...args) {
    node *n = Alloc<node>().allocate(1);
    new (&n->item) T(std::forward<Args>(args)...);
    n->next = nullptr;
    node *taild = tail.exchange(n);
    taild->next = n;
    ++__size;
    return true;
  }
  uint32_t push_bulk(const T *v, uint32_t size) {
    if (size == 0)
      return 0;
    node *first, *last, *last_prev;
    first = last_prev = Alloc<node>().allocate(1);
    new (&first->item) T(v[0]);
    for (uint32_t i = 1; i < size; ++i) {
      last = Alloc<node>().allocate(1);
      new (&last->item) T(v[i]);
      last_prev->next = last;
      last_prev = last;
    }
    last->next = nullptr;

    node *taild = tail.exchange(last);
    taild->next = first;
    __size += size;
    return size;
  }
  uint32_t push_bulk(const std::vector<T> &v) {
    return push_bulk(v.data(), v.size());
  }
  bool pop_out(T *__x) {
    copyable_pair headd;
    if (flags & ConQueueMode::F_SC_DEQ) {
      headd = head;
      node *next = const_cast<node *>(headd.first->next);
      if (next == nullptr)
        return false;
      head = copyable_pair{next, headd.second + 1};
      *__x = next->item;
      next->item.~T();
      --__size;
      Alloc<node>().deallocate(headd.first, 1);
      return true;
    } else {
      while (1) {
        headd = head;
        node *next = const_cast<node *>(headd.first->next);
        if (next == nullptr)
          return false;

        if (head.compare_exchange_weak(headd,
                                       copyable_pair{next, headd.second + 1})) {
          *__x = next->item;
          next->item.~T();
          --__size;
          Alloc<node>().deallocate(headd.first, 1);
          return true;
        }
        std::this_thread::yield();
      }
    }
  }
  uint32_t pop_out_bulk(T *v, uint32_t count) {
    uint32_t l;
    copyable_pair headd;
    if (flags & ConQueueMode::F_SC_DEQ) {
      headd = head;
      node *next = const_cast<node *>(headd.first->next),
           *next_last = headd.first;
      for (l = 0; l < count && next_last->next != nullptr;
           next_last = const_cast<node *>(next_last->next), ++l)
        ;
      head = copyable_pair{next_last, headd.second + 1};
      node *p = headd.first;
      for (uint32_t i = 0; i < l; ++i) {
        v[i] = p->next->item;
        p->next->item.~T();
        auto tmp = const_cast<node *>(p->next);
        Alloc<node>().deallocate(p, 1);
        p = tmp;
      }
      __size -= l;
      return l;
    } else {
      while (1) {
        headd = head;
        node *next = const_cast<node *>(headd.first->next),
             *next_last = headd.first;
        for (l = 0; l < count && next_last->next != nullptr;
             next_last = const_cast<node *>(next_last->next), ++l)
          ;
        if (head.compare_exchange_weak(
                headd, copyable_pair{next_last, headd.second + 1})) {
          node *p = headd.first;
          for (uint32_t i = 0; i < l; ++i) {
            v[i] = p->next->item;
            p->next->item.~T();
            auto tmp = const_cast<node *>(p->next);
            Alloc<node>().deallocate(p, 1);
            p = tmp;
          }
          __size -= l;
          return l;
        }
        std::this_thread::yield();
      }
    }
    return l;
  }
  std::vector<T> pop_out_bulk(uint32_t count) {
    std::vector<T> v;
    uint32_t l;
    v.reserve(count);
    l = pop_out_bulk(v.data(), count);
    v.assign(v.data(), v.data() + l);
    return v;
  }

private:
  uint32_t flags;
  std::atomic<uint32_t> __size;
  std::atomic<node *> tail;
  std::atomic<copyable_pair> head;
};

#endif // __CONQUEUE_H__