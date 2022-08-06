/**
 * @file extend_mutex.h

 * @brief POSIX Lock extensions for C++

 * @version 0.1
 * @date 2022-05-27
 */

#ifndef __EXTEND_MUTEX_H__
#define __EXTEND_MUTEX_H__

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

class spin_mutex_64 {
public:
  spin_mutex_64() : l(0) {}

  void lock(uint8_t i) {
    while (1) {
      uint64_t _l = __atomic_fetch_or(&l, 1UL << i, __ATOMIC_SEQ_CST);
      if ((_l & (1UL << i)) == 0)
        break;
      std::this_thread::yield();
    }
  }
  bool try_lock(uint8_t i) {
    uint64_t _l = __atomic_fetch_or(&l, 1UL << i, __ATOMIC_SEQ_CST);
    return (_l & (1UL << i)) == 0;
  }
  void unlock(uint8_t i) { __atomic_fetch_xor(&l, 1UL << i, __ATOMIC_SEQ_CST); }

private:
  uint64_t l;
};

class spin_mutex_8 {
public:
  spin_mutex_8() : l(0) {}

  void lock(uint8_t i) {
    while (1) {
      uint8_t _l = __atomic_fetch_or(&l, 1 << i, __ATOMIC_SEQ_CST);
      if ((_l & (1 << i)) == 0)
        break;
      std::this_thread::yield();
    }
  }
  bool try_lock(uint8_t i) {
    uint8_t _l = __atomic_fetch_or(&l, 1U << i, __ATOMIC_SEQ_CST);
    return (_l & (1U << i)) == 0;
  }
  void unlock(uint8_t i) { __atomic_fetch_xor(&l, 1U << i, __ATOMIC_SEQ_CST); }

private:
  uint8_t l;
};

class shared_mutex_u8 {
public:
  shared_mutex_u8() : l(0) {}

  void lock() {
    uint8_t _l = 0;
    while (!__atomic_compare_exchange_n(&l, &_l, 1, true, __ATOMIC_SEQ_CST,
                                        __ATOMIC_SEQ_CST)) {
      _l = 0;
      std::this_thread::yield();
    }
  }
  void lock_shared() {
    uint8_t _l = __atomic_fetch_add(&l, 2, __ATOMIC_SEQ_CST);
    while (_l & 1) {
      std::this_thread::yield();
      _l = __atomic_load_n(&l, __ATOMIC_SEQ_CST);
    }
  }
  bool try_lock() {
    uint8_t _l = 0;
    return __atomic_compare_exchange_n(&l, &_l, 1, true, __ATOMIC_SEQ_CST,
                                       __ATOMIC_SEQ_CST);
  }
  bool try_lock_shared() {
    uint8_t _l = __atomic_fetch_add(&l, 2, __ATOMIC_SEQ_CST);
    if (_l & 1) {
      __atomic_fetch_sub(&l, 2, __ATOMIC_SEQ_CST);
      return false;
    }
    return true;
  }
  void unlock() {
    uint8_t _l = __atomic_load_n(&l, __ATOMIC_SEQ_CST);
    if (_l & 1) {
      __atomic_xor_fetch(&l, 1, __ATOMIC_SEQ_CST);
    } else {
      __atomic_fetch_sub(&l, 2, __ATOMIC_SEQ_CST);
    }
  }

private:
  uint8_t l;
};

#endif // __EXTEND_MUTEX_H__