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

inline int rol(int in, int x) {
  int res;
  __asm__ __volatile__("rol  %%eax, %%cl" : "=a"(res) : "a"(in), "c"(x));
  return res;
}

inline int ror(int in, int x) {
  int res;
  __asm__ __volatile__("ror  %%eax, %%cl" : "=a"(res) : "a"(in), "c"(x));
  return res;
}

inline void barrier() { __asm__ __volatile__("" : : : "memory"); }

template <typename T>
inline T
atomic_max(std::atomic<T> &e, T val,
           std::memory_order s = std::memory_order::memory_order_seq_cst) {
  T _e = e.load();
  while (!e.compare_exchange_weak(_e, std::max(_e, val), s)) {
  }
  return _e;
}

template <typename T>
inline T
atomic_min(std::atomic<T> &e, T val,
           std::memory_order s = std::memory_order::memory_order_seq_cst) {
  T _e = e.load();
  while (!e.compare_exchange_weak(_e, std::min(_e, val), s)) {
  }
  return _e;
}

template <typename T>
inline T
atomic_max(volatile T *e, T val,
           std::memory_order s = std::memory_order::memory_order_seq_cst) {
  T _e = *e;
  while (!__atomic_compare_exchange_weak(e, _e, std::max(_e, val), s)) {
  }
  return std::max(_e, val);
}

template <typename T>
inline T
atomic_min(volatile T *e, T val,
           std::memory_order s = std::memory_order::memory_order_seq_cst) {
  T _e = *e;
  while (!__atomic_compare_exchange_weak(e, _e, std::min(_e, val), s)) {
  }
  return std::min(_e, val);
}

template <typename T, typename P>
inline bool
atomic_store_if(std::atomic<T> &e, T val, P fn,
                std::memory_order s = std::memory_order::memory_order_seq_cst) {
  T _e = e.load(int(s));
  while (fn(_e, val)) {
    if (e.compare_exchange_weak(_e, val, int(s))) {
      return true;
    }
  }
  return false;
}

template <typename T, typename P>
inline bool
atomic_store_if(volatile T *e, T val, P fn,
                std::memory_order s = std::memory_order::memory_order_seq_cst) {
  T _e = *e;
  while (fn(_e, val)) {
    if (__atomic_compare_exchange_weak(e, _e, val, int(s))) {
      return true;
    }
  }
  return false;
}

template <typename T>
inline T atomic_store_as_func(
    std::atomic<T> &e, T (*fn)(T e),
    std::memory_order s = std::memory_order::memory_order_seq_cst) {
  T _e = e.load(int(s)), t;
  do {
    t = fn(_e);
  } while (e.compare_exchange_weak(_e, t, int(s)));
  return _e;
}

template <typename T>
inline T atomic_store_as_func(
    volatile T *e, T (*fn)(T e),
    std::memory_order s = std::memory_order::memory_order_seq_cst) {
  T _e = *e, t;
  do {
    t = fn(_e);
  } while (__atomic_compare_exchange_weak(e, _e, t, int(s)));
  return _e;
}

#endif // __FATOMIC_H__