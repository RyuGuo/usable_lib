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

class barrier_t {
  pthread_barrier_t b;

public:
  barrier_t() { pthread_barrier_init(&b, nullptr, 0); }
  barrier_t(unsigned int count) { pthread_barrier_init(&b, nullptr, count); }
  ~barrier_t() { pthread_barrier_destroy(&b); }

  void wait() { pthread_barrier_wait(&b); }
};

template <typename D> class __spin_mutex_impl {
public:
  __spin_mutex_impl() : l(0) {}

  void lock(uint8_t i) {
    while (!try_lock(i)) {
      std::this_thread::yield();
    }
  }

  void lock() { lock(0); }

  bool try_lock(uint8_t i) {
    if (check_lock(l.load(std::memory_order_relaxed), i))
      return false;
    D l_ = l.fetch_or(1UL << i, std::memory_order_acquire);
    return !check_lock(l_, i);
  }

  bool try_lock() { return try_lock(0); }

  void unlock(uint8_t i) { l.fetch_xor(1UL << i, std::memory_order_release); }

  void unlock() { unlock(0); }

private:
  static bool check_lock(D l_, uint8_t i) { return (l_ & (1UL << i)) != 0; }

  std::atomic<D> l;
};

using spin_mutex_b8 = __spin_mutex_impl<uint8_t>;
using spin_mutex_b16 = __spin_mutex_impl<uint16_t>;
using spin_mutex_b32 = __spin_mutex_impl<uint32_t>;
using spin_mutex_b64 = __spin_mutex_impl<uint64_t>;

class ticket_mutex {
public:
  void lock() {
    uint16_t ct = v.h.fetch_add(1, std::memory_order_acquire);
    while (v.l.load(std::memory_order_acquire) != ct) {
      std::this_thread::yield();
    }
  }

  bool try_lock() {
    raw_val_t ov, nv;
    ov.raw = v.raw.load(std::memory_order_relaxed);
    if (ov.h != ov.l)
      return false;
    nv = ov;
    ++nv.h;
    return v.raw.compare_exchange_weak(ov.raw, nv.raw,
                                       std::memory_order_acquire);
  }

  void unlock() { v.l.fetch_add(1, std::memory_order_release); }

private:
  union atomic_val_t {
    struct {
      std::atomic<uint16_t> h;
      std::atomic<uint16_t> l;
    };
    std::atomic<uint32_t> raw;

    atomic_val_t() : raw(0) {}
  };

  union raw_val_t {
    struct {
      uint16_t h;
      uint16_t l;
    };
    uint32_t raw;
  };

  atomic_val_t v;
};

// high read priority
template <typename D> class __shared_mutex_impl {
public:
  static constexpr size_t size = sizeof(D);

  __shared_mutex_impl() : l(0) {}

  void lock() {
    while (!try_lock()) {
      std::this_thread::yield();
    }
  }

  void lock_shared() {
    D l_ = l.fetch_add(2, std::memory_order_acquire);
    while (l_ & 1) {
      std::this_thread::yield();
      l_ = l.load(std::memory_order_acquire);
    }
  }

  bool try_lock() {
    D l_ = 0;
    return l.compare_exchange_weak(l_, 1, std::memory_order_acquire);
  }

  bool try_lock_shared() {
    D l_ = l.fetch_add(2, std::memory_order_acquire);
    if (l_ & 1) {
      l.fetch_sub(2, std::memory_order_release);
      return false;
    }
    return true;
  }

  void unlock() { l.fetch_xor(1, std::memory_order_release); }

  void unlock_shared() { l.fetch_sub(2, std::memory_order_release); }

private:
  std::atomic<D> l;
};

using shared_mutex_b8 = __shared_mutex_impl<uint8_t>;
using shared_mutex_b16 = __shared_mutex_impl<uint16_t>;
using shared_mutex_b32 = __shared_mutex_impl<uint32_t>;
using shared_mutex_b64 = __shared_mutex_impl<uint64_t>;

template <typename D> struct __intention_mutex_impl {
  __intention_mutex_impl() : l(0) {}

  void lock_shared() {
    D l_ = l.load(std::memory_order_acquire);
    while (1) {
      if (l_ & 1) {
        std::this_thread::yield();
        l_ = l.load(std::memory_order_acquire);
      } else if (l.compare_exchange_weak(l_, l_ + 2,
                                         std::memory_order_acquire)) {
        break;
      }
    }
  }

  bool try_lock_prompt() {
    D l_ = l.fetch_or(1, std::memory_order_acquire);
    if (l_ & 1)
      return false;
    unlock_shared();
    while (l.load(std::memory_order_acquire) & ~(1UL)) {
      std::this_thread::yield();
    }
    return true;
  }

  void unlock() { l.store(0, std::memory_order_release); }

  void unlock_shared() { l.fetch_sub(2, std::memory_order_release); }

private:
  std::atomic<D> l;
};

using intention_mutex_b8 = __intention_mutex_impl<uint8_t>;
using intention_mutex_b16 = __intention_mutex_impl<uint16_t>;
using intention_mutex_b32 = __intention_mutex_impl<uint32_t>;
using intention_mutex_b64 = __intention_mutex_impl<uint64_t>;

#endif // __EXTEND_MUTEX_H__