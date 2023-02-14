/**
 * @file extend_mutex.h

 * @brief POSIX Lock extensions for C++

 * @version 0.1
 * @date 2022-05-27
 */

#ifndef __EXTEND_MUTEX_H__
#define __EXTEND_MUTEX_H__

#include "extend_new.h"
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

template <typename D> class __spin_mutex_impl {
public:
  static constexpr size_t size = sizeof(D);

  __spin_mutex_impl() : l(0) {}

  void lock(uint8_t i) {
    while (1) {
      D _l = l.fetch_or(1UL << i, std::memory_order_acquire);
      if ((_l & (1UL << i)) == 0)
        break;
      std::this_thread::yield();
    }
  }
  void lock() { lock(0); }
  bool try_lock(uint8_t i) {
    D _l = l.fetch_or(1UL << i, std::memory_order_acquire);
    return (_l & (1UL << i)) == 0;
  }
  bool try_lock() { return try_lock(0); }
  void unlock(uint8_t i) { l.fetch_xor(1UL << i, std::memory_order_release); }
  void unlock() { unlock(0); }

private:
  std::atomic<D> l;
};

using spin_mutex_b8 = __spin_mutex_impl<uint8_t>;
using spin_mutex_b16 = __spin_mutex_impl<uint16_t>;
using spin_mutex_b32 = __spin_mutex_impl<uint32_t>;
using spin_mutex_b64 = __spin_mutex_impl<uint64_t>;

class ticket_mutex {
public:
  void lock() {
    uint32_t ct = l.fetch_add(1u << 16, std::memory_order_acquire);
    uint16_t t = ct >> 16;
    while (t != (ct & 0xffff)) {
      std::this_thread::yield();
      ct = l.load(std::memory_order_acquire);
    }
  }
  bool try_lock() {
    uint32_t ct = l.load(std::memory_order_acquire);
    return (ct >> 16) == (ct & 0xffff) &&
           l.compare_exchange_weak(ct, ct + (1u << 16),
                                   std::memory_order_acquire);
  }
  void unlock() { l.fetch_add(1, std::memory_order_release); }

private:
  // h16: ticket, l16: cur_tick
  std::atomic<uint32_t> l;
};

template <typename D> class __shared_mutex_impl {
public:
  static constexpr size_t size = sizeof(D);

  __shared_mutex_impl() : l(0) {}

  void lock() {
    D _l = 0;
    while (!l.compare_exchange_weak(_l, 1, std::memory_order_acquire)) {
      _l = 0;
      std::this_thread::yield();
    }
  }
  void lock_shared() {
    D _l = l.fetch_add(2, std::memory_order_acquire);
    while (_l & 1) {
      std::this_thread::yield();
      _l = l.load(std::memory_order_relaxed);
    }
  }
  bool try_lock() {
    D _l = 0;
    return l.compare_exchange_weak(_l, 1, std::memory_order_acquire);
  }
  bool try_lock_shared() {
    D _l = l.fetch_add(2, std::memory_order_acquire);
    if (_l & 1) {
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

struct intention_mutex {
  intention_mutex() : l(0) {}

  void lock_shared() {
    uint8_t o = l.load(std::memory_order_acquire);
    while (1) {
      if (o & 1) {
        std::this_thread::yield();
        o = l.load(std::memory_order_acquire);
      } else if (l.compare_exchange_weak(o, o + 2, std::memory_order_acquire)) {
        break;
      }
    }
  }

  bool try_lock_prompt() {
    uint8_t o = l.fetch_or(1, std::memory_order_acquire);
    if (o & 1)
      return false;
    unlock_shared();
    while (l.load(std::memory_order_acquire) & ~(1)) {
    }
    return true;
  }

  void unlock() { l.store(0, std::memory_order_release); }
  void unlock_shared() { l.fetch_sub(2, std::memory_order_release); }

private:
  std::atomic<uint8_t> l;
};

class mcs_mutex {
public:
  mcs_mutex() : tail(nullptr) {}

  void lock() {
    mcs_node &my_node = my_node_spe;
    mcs_node *pred = tail.exchange(&my_node, std::memory_order_acquire);
    if (pred != nullptr) {
      my_node.locked = true;
      pred->next = &my_node;
      while (my_node.locked) {
        std::this_thread::yield();
      }
    }
  }
  bool try_lock() {
    mcs_node &my_node = my_node_spe;
    mcs_node *node_ptr = nullptr;
    return tail.compare_exchange_weak(node_ptr, &my_node,
                                      std::memory_order_acquire);
  }
  void unlock() {
    mcs_node &my_node = my_node_spe;
    mcs_node *node_ptr = &my_node;
    if (my_node.next == nullptr) {
      if (tail.compare_exchange_weak(node_ptr, nullptr,
                                     std::memory_order_release))
        return;
      while (my_node.next == nullptr) {
        std::this_thread::yield();
      }
    }
    my_node.next->locked = false;
    my_node.next = nullptr;
  }

private:
  struct mcs_node {
    volatile bool locked = false;
    volatile mcs_node *next = nullptr;
  } __attribute__((aligned(64)));

  local_thread_specific<mcs_node> my_node_spe __attribute__((aligned(64)));
  std::atomic<mcs_node *> tail;
};

template <typename Mutex> class SingleFlight {
public:
  template <typename F, typename... Args> void call(F &&fn, Args &&...args) {
    if (mu.try_lock()) {
      fn(std::forward<Args>(args)...);
    } else {
      mu.lock();
    }
    mu.unlock();
  }

private:
  Mutex mu;
};

#endif // __EXTEND_MUTEX_H__