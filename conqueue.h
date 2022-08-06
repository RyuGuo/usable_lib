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
                            ConQueueMode::F_MC_HTS_DEQ |
                            ConQueueMode::F_EXACT_SZ)
      : ring(ring), flags(flags), h(0), h_(0), t(0), t_(0), _max_size(max_size),
        self_alloc(false) {
    if (ring == nullptr)
      throw "ring array is null";
    if (max_size == 0)
      throw "max_size is 0";
    if ((flags & F_SP_ENQ) &&
        ((flags & F_MP_RTS_ENQ) || (flags & F_MP_HTS_ENQ)))
      throw "queue producer flag error";
    if ((flags & F_SC_DEQ) &&
        ((flags & F_MC_RTS_DEQ) || (flags & F_MC_HTS_DEQ)))
      throw "queue consumer flag error";
    if ((flags & ConQueueMode::F_EXACT_SZ) == false) {
      max_size |= max_size >> 1;
      max_size |= max_size >> 2;
      max_size |= max_size >> 4;
      max_size |= max_size >> 8;
      max_size |= max_size >> 16;
    }
    count = max_size;
  }
  ConQueue(uint32_t max_size, uint32_t flags = ConQueueMode::F_MP_HTS_ENQ |
                                               ConQueueMode::F_MC_HTS_DEQ |
                                               ConQueueMode::F_EXACT_SZ)
      : ConQueue(Alloc().allocate(max_size * sizeof(T)), max_size, flags) {
    self_alloc = true;
  }
  ~ConQueue() {
    uint32_t _h = h, _t = t;
    for (uint32_t i = _t; i != _h; ++i) {
      ring[to_id(i)].~T();
    }
    if (self_alloc)
      Alloc().deallocate(ring, _max_size);
  }
  bool empty() { return h_ == t_; }
  bool full() { return h_ - t_ == _max_size; }
  uint32_t size() { return h_ - t_; }
  uint32_t capacity() { return _max_size; }
  // Guarantee that the reference will not be poped
  T &front() { return ring[t_]; }
  void clear() { pop_out_bulk(nullptr, size()); }
  bool push(const T &__x) {
    if (flags & ConQueueMode::F_SP_ENQ) {
      if (h - t_ == _max_size)
        return false;
      ::new (&ring[to_id(h)]) T(__x);
      ++h;
      ++h_;
    } else {
      uint32_t _h = h, _h_;
      do {
        if (_h - t_ == _max_size)
          return false;
      } while (!h.compare_exchange_weak(
          _h, _h + 1, std::memory_order::memory_order_relaxed));
      ::new (&ring[to_id(_h)]) T(__x);
      // Wait for other threads to finish copying
      _h_ = _h;
      while (!h_.compare_exchange_weak(
          _h_, _h_ + 1, std::memory_order::memory_order_relaxed)) {
        std::this_thread::yield();
        _h_ = _h;
      }
    }
    return true;
  }
  /**
   * @brief
   *
   * @param v
   * @param size
   * @return The number of successful elem
   */
  uint32_t push_bulk(const T *v, uint32_t size) {
    uint32_t l;
    if (flags & ConQueueMode::F_SP_ENQ) {
      l = std::min(_max_size - h + t_, size);
      for (uint32_t i = 0; i < l; ++i) {
        ::new (&ring[to_id(h + i)]) T(v[i]);
      }
      h += l;
      h_ += l;
    } else {
      uint32_t _h = h, _h_;
      do {
        l = std::min(_max_size - _h + t_, size);
      } while (!h.compare_exchange_weak(
          _h, _h + l, std::memory_order::memory_order_relaxed));
      for (uint32_t i = 0; i < l; ++i) {
        ::new (&ring[to_id(_h + i)]) T(v[i]);
      }
      // Wait for other threads to finish copying
      _h_ = _h;
      while (!h_.compare_exchange_weak(
          _h_, _h_ + l, std::memory_order::memory_order_relaxed)) {
        std::this_thread::yield();
        _h_ = _h;
      }
    }
    return l;
  }
  uint32_t push_bulk(const std::vector<T> &v) {
    return push_bulk(v.data(), v.size());
  }
  bool pop_out(T *__x) {
    if (flags & ConQueueMode::F_SC_DEQ) {
      if (t == h_)
        return false;
      *__x = ring[to_id(t)];
      ring[to_id(t)].~T();
      ++t;
      ++t_;
    } else {
      uint32_t _t = t, _t_;
      do {
        if (_t == h_)
          return false;
      } while (!t.compare_exchange_weak(
          _t, _t + 1, std::memory_order::memory_order_relaxed));
      *__x = ring[to_id(_t)];
      ring[to_id(_t)].~T();
      // Wait for other threads to finish copying
      _t_ = _t;
      while (!t_.compare_exchange_weak(
          _t_, _t_ + 1, std::memory_order::memory_order_relaxed)) {
        std::this_thread::yield();
        _t_ = _t;
      }
    }
    return true;
  }
  uint32_t pop_out_bulk(T *v, uint32_t count) {
    uint32_t l = 0;
    if (flags & ConQueueMode::F_SC_DEQ) {
      while (count != 0 && t < h_) {
        if (v) {
          v[l] = ring[to_id(t + l)];
        }
        ring[to_id(t + l)].~T();
        --count;
        ++l;
      }
      t += l;
      t_ += l;
    } else {
      uint32_t _t = t, _t_, c;
      do {
        c = std::min(count, h_ - _t);
      } while (!t.compare_exchange_weak(
          _t, c + _t, std::memory_order::memory_order_relaxed));
      if (c == 0)
        return 0;
      while (c != 0) {
        if (v) {
          v[l] = ring[to_id(_t + l)];
        }
        ring[to_id(_t + l)].~T();
        --c;
        ++l;
      }
      // Wait for other threads to finish copying
      _t_ = _t;
      while (!t_.compare_exchange_weak(
          _t_, _t_ + l, std::memory_order::memory_order_relaxed)) {
        std::this_thread::yield();
        _t_ = _t;
      }
    }
    return l;
  }
  std::vector<T> pop_out_bulk(uint32_t count) {
    std::vector<T> v;
    uint32_t l = 0;
    v.reserve(count);
    l = pop_out_bulk(v.data(), count);
    v.assign(v.data(), v.data() + l);
    return v;
  }

private:
  /**
   *   t   --->  h
   * [][][][][][][]
   */
  T *ring;
  bool self_alloc;
  std::atomic<uint32_t> h, h_;
  std::atomic<uint32_t> t, t_;
  uint32_t flags;
  /**
   * if F_EXACT_SZ: `count` is actual size
   * else:          `count` is minmax 2^n - 1
   */
  uint32_t count;
  uint32_t _max_size;

  uint32_t to_id(uint32_t i) {
    return (flags & ConQueueMode::F_EXACT_SZ) ? (i % count) : (i & count);
  }
};

template <typename T> class ConQueueEX {
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
                              ConQueueMode::F_MC_HTS_DEQ |
                              ConQueueMode::F_EXACT_SZ)
      : flags(flags), __size(0), head(copyable_pair{nullptr, 0}) {
    node *dummy = pl_allocator<node>().allocate(1);
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
    node *n = pl_allocator<node>().allocate(1);
    ::new (&n->item) T(__x);
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
    first = last_prev = pl_allocator<node>().allocate(1);
    ::new (&first->item) T(v[0]);
    for (uint32_t i = 1; i < size; ++i) {
      last = pl_allocator<node>().allocate(1);
      ::new (&last->item) T(v[i]);
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
      pl_allocator<node>().deallocate(headd.first, 1);
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
          pl_allocator<node>().deallocate(headd.first, 1);
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
        if (v) {
          v[i] = p->next->item;
        }
        p->next->item.~T();
        auto tmp = const_cast<node *>(p->next);
        pl_allocator<node>().deallocate(p, 1);
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
            if (v) {
              v[i] = p->next->item;
            }
            p->next->item.~T();
            auto tmp = const_cast<node *>(p->next);
            pl_allocator<node>().deallocate(p, 1);
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