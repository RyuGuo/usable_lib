/**
 * @file conqueue.h
 * @brief lock-free concurrent queue
 * @version 0.1
 * @date 2022-06-12
 */

#ifndef __CONQUEUE_H__
#define __CONQUEUE_H__

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
  /**
   *   t   --->  h
   * [][][][][][][]
   */
  T *ring;
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

public:
  ConQueue(uint32_t max_size, uint32_t flags = ConQueueMode::F_MP_HTS_ENQ |
                                               ConQueueMode::F_MC_HTS_DEQ |
                                               ConQueueMode::F_EXACT_SZ)
      : flags(flags), h(0), h_(0), t(0), t_(0), _max_size(max_size) {
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
    ring = Alloc().allocate(max_size * sizeof(T));
  }
  ~ConQueue() {
    uint32_t h = h_, t = t_;
    for (uint32_t i = h; i != t; --i) {
      ring[to_id(i - 1)].~T();
    }
    Alloc().deallocate(ring, _max_size);
  }
  bool empty() { return h_ == t_; }
  bool full() { return h_ - t_ == _max_size; }
  uint32_t size() { return h_ - t_; }
  // Guarantee that the reference will not be poped
  T &front() { return ring[t_]; }
  void push(const T &__x) {
    if (flags & ConQueueMode::F_SP_ENQ) {
      if (h - t_ == _max_size)
        throw "queue full";
      ::new (&ring[to_id(h)]) T(__x);
      ++h;
      ++h_;
    } else {
      uint32_t _h = h, _h_;
      do {
        if (_h - t_ == _max_size)
          throw "queue full";
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
  }
  void push_burst(const T *v, size_t size) {
    if (flags & ConQueueMode::F_SP_ENQ) {
      if (h - t_ + size > _max_size)
        throw "queue full";
      for (uint32_t i = 0; i < size; ++i) {
        ::new (&ring[to_id(h + i)]) T(v[i]);
      }
      h += size;
      h_ += size;
    } else {
      uint32_t _h = h, _h_;
      do {
        if (_h - t_ + size > _max_size)
          throw "queue full";
      } while (!h.compare_exchange_weak(
          _h, _h + size, std::memory_order::memory_order_relaxed));
      for (uint32_t i = 0; i < size; ++i) {
        ::new (&ring[to_id(_h + i)]) T(v[i]);
      }
      // Wait for other threads to finish copying
      _h_ = _h;
      while (!h_.compare_exchange_weak(
          _h_, _h_ + size, std::memory_order::memory_order_relaxed)) {
        std::this_thread::yield();
        _h_ = _h;
      }
    }
  }
  void push_burst(const std::vector<T> &v) { push_burst(v.data(), v.size()); }
  T pop_out() {
    if (flags & ConQueueMode::F_SC_DEQ) {
      if (t == h_)
        throw "queue empty";
      T __x = ring[to_id(t)];
      ring[to_id(t)].~T();
      ++t;
      ++t_;
      return __x;
    } else {
      uint32_t _t = t, _t_;
      do {
        if (_t == h_)
          throw "queue empty";
      } while (!t.compare_exchange_weak(
          _t, _t + 1, std::memory_order::memory_order_relaxed));
      T __x = ring[to_id(_t)];
      ring[to_id(_t)].~T();
      // Wait for other threads to finish copying
      _t_ = _t;
      while (!t_.compare_exchange_weak(
          _t_, _t_ + 1, std::memory_order::memory_order_relaxed)) {
        std::this_thread::yield();
        _t_ = _t;
      }
      return __x;
    }
  }
  uint32_t pop_out_burst(T *v, uint32_t count) {
    uint32_t l = 0;
    if (flags & ConQueueMode::F_SC_DEQ) {
      while (count != 0 && t < h_) {
        v[l] = ring[to_id(t + l)];
        ring[to_id(t + l)].~T();
        --count;
        ++l;
      }
      t += l;
      t_ += l;
    } else {
      uint32_t _t = t, _t_, c;
      do {
        c = std::min(count, h_.load() - _t);
      } while (!t.compare_exchange_weak(
          _t, c + _t, std::memory_order::memory_order_relaxed));
      while (c != 0) {
        v[l] = ring[to_id(_t + l)];
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
  template <typename... Args, typename _Alloc = std::allocator<T>>
  std::vector<T, _Alloc> pop_out_burst(uint32_t count,
                                       Args &&...construct_args) {
    std::vector<T, _Alloc> v(count, T(std::forward<Args>(construct_args)...));
    uint32_t l = pop_out_burst(v.data(), count);
    v.resize(l);
    return v;
  }
};

#endif // __CONQUEUE_H__