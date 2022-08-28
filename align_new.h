#ifndef __NEW_H__
#define __NEW_H__

#include <cstdlib>
#include <utility>

template <typename T, size_t ALIGN, typename... Args>
T *aligned_new(size_t n, Args &&...construct_args) {
  T *mr = (T *)aligned_alloc(ALIGN, n * sizeof(T));
  for (size_t i = 0; i < n; ++i) {
    new (mr + i) T(std::forward<Args>(construct_args)...);
  }
  return mr;
}

template <typename T> void aligned_delete(T *ptr, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    ptr[i].~T();
  }
  free(ptr);
}

#endif // __NEW_H__