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

#endif // __EXTEND_MUTEX_H__