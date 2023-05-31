#ifndef __EXTEND_NEW_H__
#define __EXTEND_NEW_H__

#include <cstdlib>
#include <pthread.h>
#include <utility>

template <typename T, size_t ALIGN, typename... Args>
T *aligned_new(size_t n, Args &&...construct_args) {
  T *mr = (T *)aligned_alloc(ALIGN, n * sizeof(T));
  for (size_t i = 0; i < n; ++i)
    new (mr + i) T(std::forward<Args>(construct_args)...);
  return mr;
}

template <typename T> void aligned_delete(T *ptr, size_t n) {
  for (size_t i = 0; i < n; ++i)
    ptr[i].~T();
  free(ptr);
}

#define flexible_array(T, ft_name)                                             \
  __flexible_array_impl__<                                                     \
      T, typename std::remove_reference<decltype(*T::ft_name)>::type,          \
      offsetof(T, ft_name)>

template <typename T, typename FT, size_t FT_OFFSET>
struct __flexible_array_impl__ final : public T {
  void *operator new(size_t, size_t n) {
    void *p = ::operator new(sizeof(size_t) + FT_OFFSET + n * sizeof(FT));
    size_t *size_ptr = reinterpret_cast<size_t *>(p);
    *size_ptr = n;
    return reinterpret_cast<void *>(reinterpret_cast<char *>(p) +
                                    sizeof(size_t));
  }
  void operator delete(void *p) {
    void *_p =
        reinterpret_cast<void *>(reinterpret_cast<char *>(p) - sizeof(size_t));
    ::operator delete(_p);
  }

  __flexible_array_impl__(T &&__x, FT &&__y) : T(std::forward<T>(__x)) {
    size_t *size_ptr = reinterpret_cast<size_t *>(
        reinterpret_cast<char *>(this) - sizeof(size_t));
    size_t n = *size_ptr;
    FT *fla_ptr =
        reinterpret_cast<FT *>(reinterpret_cast<char *>(this) + FT_OFFSET);
    for (size_t i = 0; i < n; ++i)
      new (&fla_ptr[i]) FT(std::forward<FT>(__y));
  }
  ~__flexible_array_impl__() {
    size_t *size_ptr = reinterpret_cast<size_t *>(
        reinterpret_cast<char *>(this) - sizeof(size_t));
    size_t n = *size_ptr;
    FT *fla_ptr =
        reinterpret_cast<FT *>(reinterpret_cast<char *>(this) + FT_OFFSET);
    for (size_t i = 0; i < n; ++i)
      fla_ptr[i].~FT();
  }
};

template <typename T> class thread_local_specific {
public:
  template <typename... Args> thread_local_specific() {
    pthread_key_create(&pkey, dtor_func);
  }
  ~thread_local_specific() { pthread_key_delete(pkey); }

  thread_local_specific(const thread_local_specific &) = delete;
  thread_local_specific(thread_local_specific &&) = delete;
  thread_local_specific &operator=(const thread_local_specific &) = delete;
  thread_local_specific &operator=(thread_local_specific &&) = delete;

  T &set(const T &x) {
    T *ptr = reinterpret_cast<T *>(pthread_getspecific(pkey));
    if (__glibc_unlikely(ptr == nullptr)) {
      ptr = new T(x);
      pthread_setspecific(pkey, ptr);
    } else {
      *ptr = x;
    }
    return *ptr;
  }
  T &set(T &&x) {
    T *ptr = reinterpret_cast<T *>(pthread_getspecific(pkey));
    if (__glibc_unlikely(ptr == nullptr)) {
      ptr = new T(std::forward<T>(x));
      pthread_setspecific(pkey, ptr);
    } else {
      *ptr = std::forward<T>(x);
    }
    return *ptr;
  }
  T &get() {
    T *ptr = reinterpret_cast<T *>(pthread_getspecific(pkey));
    if (__glibc_unlikely(ptr == nullptr)) {
      ptr = new T();
      pthread_setspecific(pkey, ptr);
    }
    return *ptr;
  }

  T &operator=(const T &x) { return set(x); }
  T &operator=(T &&x) { return set(std::move(x)); }
  operator T &() { return get(); }

private:
  pthread_key_t pkey;

  static void dtor_func(void *p) {
    T *ptr = reinterpret_cast<T *>(p);
    if (ptr)
      delete ptr;
  }
};

#endif // __EXTEND_NEW_H__