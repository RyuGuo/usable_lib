/**
 * @file extend_mutex.h

 * @brief POSIX Lock extensions for C++

 * @version 0.1
 * @date 2022-05-27
 */

#ifndef __EXTEND_MUTEX_H__
#define __EXTEND_MUTEX_H__

#include <atomic>
#include <pthread.h>
#include <thread>

class shared_mutex {
  pthread_rwlock_t rwlock;

public:
  shared_mutex() { pthread_rwlock_init(&rwlock, nullptr); }
  ~shared_mutex() { pthread_rwlock_destroy(&rwlock); }

  void lock_shared() { pthread_rwlock_rdlock(&rwlock); }
  bool try_lock_shared() { return pthread_rwlock_tryrdlock(&rwlock) == 0; }
  bool try_lock() { return pthread_rwlock_trywrlock(&rwlock) == 0; }
  void lock() { pthread_rwlock_wrlock(&rwlock); }
  void unlock() { pthread_rwlock_unlock(&rwlock); }
};

class spin_mutex {
  pthread_spinlock_t spinlock;

public:
  spin_mutex(int pshared = PTHREAD_PROCESS_PRIVATE) {
    pthread_spin_init(&spinlock, pshared);
  }
  ~spin_mutex() { pthread_spin_destroy(&spinlock); }

  void lock() { pthread_spin_lock(&spinlock); }
  bool try_lock() { return pthread_spin_trylock(&spinlock) == 0; }
  void unlock() { pthread_spin_unlock(&spinlock); }
};

class spin_condition_variable {
public:
  template <typename _Predicate> void wait(_Predicate __p) {
    while (!__p())
      std::this_thread::yield();
  }
};

class barrier_t {
  pthread_barrier_t b;

public:
  barrier_t() { pthread_barrier_init(&b, nullptr, 0); }
  barrier_t(unsigned int count) { pthread_barrier_init(&b, nullptr, count); }
  ~barrier_t() { pthread_barrier_destroy(&b); }

  void init(unsigned int count) { pthread_barrier_init(&b, nullptr, count); }
  void wait() { pthread_barrier_wait(&b); }
};

class spin_mutex_u64 {
public:
  spin_mutex_u64() : l(0) {}

  void lock(uint8_t i) {
    while (1) {
      uint64_t _l = l.fetch_or(1UL << i, std::memory_order_acquire);
      if ((_l & (1UL << i)) == 0)
        break;
      std::this_thread::yield();
    }
  }
  void lock() { lock(0); }
  bool try_lock(uint8_t i) {
    uint64_t _l = l.fetch_or(1UL << i, std::memory_order_acquire);
    return (_l & (1UL << i)) == 0;
  }
  bool try_lock() { return try_lock(0); }
  void unlock(uint8_t i) { l.fetch_xor(1UL << i, std::memory_order_release); }
  void unlock() { unlock(0); }

private:
  std::atomic<uint64_t> l;
};

class spin_mutex_u8 {
public:
  spin_mutex_u8() : l(0) {}

  void lock(uint8_t i) {
    while (1) {
      uint8_t _l = l.fetch_or(1U << i, std::memory_order_acquire);
      if ((_l & (1 << i)) == 0)
        break;
      std::this_thread::yield();
    }
  }
  void lock() { lock(0); }
  bool try_lock(uint8_t i) {
    uint8_t _l = l.fetch_or(1U << i, std::memory_order_acquire);
    return (_l & (1U << i)) == 0;
  }
  bool try_lock() { return try_lock(0); }
  void unlock(uint8_t i) { l.fetch_xor(1U << i, std::memory_order_release); }
  void unlock() { unlock(0); }

private:
  std::atomic<uint8_t> l;
};

class shared_mutex_u8 {
public:
  shared_mutex_u8() : l(0) {}

  void lock() {
    uint8_t _l = 0;
    while (!l.compare_exchange_weak(_l, 1, std::memory_order_acquire)) {
      _l = 0;
      std::this_thread::yield();
    }
  }
  void lock_shared() {
    uint8_t _l = l.fetch_add(2, std::memory_order_acquire);
    while (_l & 1) {
      std::this_thread::yield();
      _l = l.load(std::memory_order_relaxed);
    }
  }
  bool try_lock() {
    uint8_t _l = 0;
    return l.compare_exchange_weak(_l, 1, std::memory_order_acquire);
  }
  bool try_lock_shared() {
    uint8_t _l = l.fetch_add(2, std::memory_order_acquire);
    if (_l & 1) {
      l.fetch_sub(2, std::memory_order_release);
      return false;
    }
    return true;
  }
  void unlock() { l.fetch_xor(1, std::memory_order_release); }
  void unlock_shared() { l.fetch_sub(2, std::memory_order_release); }

private:
  std::atomic<uint8_t> l;
};

#endif // __EXTEND_MUTEX_H__