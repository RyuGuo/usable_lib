/**
 * @file fatomic.h

 * @brief Atomic operation extensions

 * @version 0.1
 * @date 2022-05-27
 */

#ifndef __FATOMIC_H__
#define __FATOMIC_H__

#include <algorithm>
#include <atomic>
#include <thread>

inline void barrier() { __asm__ __volatile__("" : : : "memory"); }

template <typename T>
inline T
atomic_max(std::atomic<T> &e, T val,
           std::memory_order s = std::memory_order::memory_order_seq_cst,
           std::memory_order f = std::memory_order::memory_order_seq_cst) {
  T _e = e.load(s);
  while (!e.compare_exchange_weak(_e, std::max(_e, val), s, f)) {
    std::this_thread::yield();
  }
  return _e;
}

template <typename T>
inline T
atomic_min(std::atomic<T> &e, T val,
           std::memory_order s = std::memory_order::memory_order_seq_cst,
           std::memory_order f = std::memory_order::memory_order_seq_cst) {
  T _e = e.load(s);
  while (!e.compare_exchange_weak(_e, std::min(_e, val), s, f)) {
    std::this_thread::yield();
  }
  return _e;
}

template <typename T>
inline T
atomic_max(volatile T *e, T val,
           std::memory_order s = std::memory_order::memory_order_seq_cst,
           std::memory_order f = std::memory_order::memory_order_seq_cst) {
  T _e = *e;
  while (!__atomic_compare_exchange_n(e, &_e, std::max(_e, val), true, int(s),
                                      int(f))) {
    std::this_thread::yield();
  }
  return _e;
}

template <typename T>
inline T
atomic_min(volatile T *e, T val,
           std::memory_order s = std::memory_order::memory_order_seq_cst,
           std::memory_order f = std::memory_order::memory_order_seq_cst) {
  T _e = *e;
  while (!__atomic_compare_exchange_n(e, &_e, std::min(_e, val), true, int(s),
                                      int(f))) {
    std::this_thread::yield();
  }
  return _e;
}

template <typename T, typename P>
inline bool
atomic_store_if(std::atomic<T> &e, T val, P fn,
                std::memory_order s = std::memory_order::memory_order_seq_cst,
                std::memory_order f = std::memory_order::memory_order_seq_cst) {
  T _e = e.load(s);
  while (fn(_e, val)) {
    if (e.compare_exchange_weak(_e, val, s, f))
      return true;
    std::this_thread::yield();
  }
  return false;
}

template <typename T, typename P>
inline bool
atomic_store_if(volatile T *e, T val, P fn,
                std::memory_order s = std::memory_order::memory_order_seq_cst,
                std::memory_order f = std::memory_order::memory_order_seq_cst) {
  T _e = *e;
  while (fn(_e, val)) {
    if (__atomic_compare_exchange_n(e, &_e, val, true, int(s), int(f)))
      return true;
    std::this_thread::yield();
  }
  return false;
}

template <typename T, typename P>
inline T atomic_store_as_func(
    std::atomic<T> &e, P fn,
    std::memory_order s = std::memory_order::memory_order_seq_cst,
    std::memory_order f = std::memory_order::memory_order_seq_cst) {
  T _e = e.load(s);
  while (!e.compare_exchange_weak(_e, fn(_e), s, f)) {
    std::this_thread::yield();
  }
  return _e;
}

template <typename T, typename P>
inline T atomic_store_as_func(
    volatile T *e, P fn,
    std::memory_order s = std::memory_order::memory_order_seq_cst,
    std::memory_order f = std::memory_order::memory_order_seq_cst) {
  T _e = *e;
  while (!__atomic_compare_exchange_n(e, &_e, fn(_e), true, int(s), int(f))) {
    std::this_thread::yield();
  }
  return _e;
}

#endif // __FATOMIC_H__