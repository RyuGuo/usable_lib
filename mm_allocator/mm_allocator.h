#ifndef __MM_ALLOCATOR_H__
#define __MM_ALLOCATOR_H__

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace mm_allocator {
void env_init(void *__addr, size_t __max_size,
              void (*mm_sync)(void *ptr, size_t size),
              void (*mm_drain)());
void env_release();

void *mm_ptr_at(uint64_t off);

class mm_nullptr_t;

template <typename T> class mm_ptr {
  uint64_t off;
  friend class mm_nullptr_t;

public:
  mm_ptr() {}
  mm_ptr(mm_nullptr_t);
  mm_ptr &operator=(const mm_ptr &p) {
    off = p.off;
    return *this;
  }
  T *operator->() { return (T *)mm_ptr_at(off); }
  T &operator*() { return *(T *)mm_ptr_at(off); }
  mm_ptr operator+(int n) const {
    mm_ptr p;
    p.off = off + sizeof(T) * n;
    return p;
  }
  mm_ptr operator-(int n) const {
    mm_ptr p;
    p.off = off - sizeof(T) * n;
    return p;
  }
  mm_ptr operator+(uint32_t n) const {
    mm_ptr p;
    p.off = off + sizeof(T) * n;
    return p;
  }
  mm_ptr operator-(uint32_t n) const {
    mm_ptr p;
    p.off = off - sizeof(T) * n;
    return p;
  }
  mm_ptr operator+(uint64_t n) const {
    mm_ptr p;
    p.off = off + sizeof(T) * n;
    return p;
  }
  mm_ptr operator-(uint64_t n) const {
    mm_ptr p;
    p.off = off - sizeof(T) * n;
    return p;
  }
  mm_ptr operator+(int64_t n) const {
    mm_ptr p;
    p.off = off + sizeof(T) * n;
    return p;
  }
  mm_ptr operator-(int64_t n) const {
    mm_ptr p;
    p.off = off - sizeof(T) * n;
    return p;
  }
  mm_ptr &operator++() {
    off += sizeof(T);
    return *this;
  }
  mm_ptr operator++(int) {
    mm_ptr p = *this;
    off += sizeof(T);
    return p;
  }
  mm_ptr operator+=(uint64_t n) {
    mm_ptr p = *this;
    off += sizeof(T) * n;
    return p;
  }
  mm_ptr &operator--() {
    off -= sizeof(T);
    return *this;
  }
  mm_ptr operator--(int) {
    mm_ptr p = *this;
    off -= sizeof(T);
    return p;
  }
  mm_ptr operator-=(uint64_t n) {
    mm_ptr p = *this;
    off -= sizeof(T) * n;
    return p;
  }

  bool operator==(const mm_ptr &p) const { return off == p.off; }
  bool operator!=(const mm_ptr &p) const { return off != p.off; }
  bool operator==(mm_nullptr_t) const;
  bool operator!=(mm_nullptr_t) const;
  mm_ptr &operator=(mm_nullptr_t);
  operator bool() { return off != 0; }
  operator uint64_t() { return off; }
};

template <> class mm_ptr<void> {
  uint64_t off;
  friend class mm_nullptr_t;

public:
  mm_ptr() {}
  mm_ptr(mm_nullptr_t);
  mm_ptr &operator=(const mm_ptr &p) {
    off = p.off;
    return *this;
  }
  bool operator==(const mm_ptr &p) const { return off == p.off; }
  bool operator!=(const mm_ptr &p) const { return off != p.off; }
  bool operator==(mm_nullptr_t) const;
  bool operator!=(mm_nullptr_t) const;
  mm_ptr &operator=(mm_nullptr_t);
  operator bool() { return off != 0; }
  operator uint64_t() { return off; }
};

class mm_nullptr_t {
public:
  template <typename T> bool operator==(const mm_ptr<T> p) const {
    return p.off == 0;
  }
  template <typename T> bool operator!=(const mm_ptr<T> p) const {
    return p.off != 0;
  }
};

template <typename T> inline bool mm_ptr<T>::operator==(mm_nullptr_t) const {
  return off == 0;
}
template <typename T> inline bool mm_ptr<T>::operator!=(mm_nullptr_t) const {
  return off != 0;
}
template <typename T> inline mm_ptr<T>::mm_ptr(mm_nullptr_t) : off(0) {}
template <typename T> inline mm_ptr<T> &mm_ptr<T>::operator=(mm_nullptr_t) {
  off = 0;
  return *this;
}
inline bool mm_ptr<void>::operator==(mm_nullptr_t) const { return off == 0; }
inline bool mm_ptr<void>::operator!=(mm_nullptr_t) const { return off != 0; }
inline mm_ptr<void>::mm_ptr(mm_nullptr_t) : off(0) {}
inline mm_ptr<void> &mm_ptr<void>::operator=(mm_nullptr_t) {
  off = 0;
  return *this;
}

template <typename T, typename R> mm_ptr<T> mm_cast(mm_ptr<R> p) {
  return *(mm_ptr<T> *)(&p);
}

static const mm_nullptr_t mm_nullptr;

mm_ptr<void> mm_alloc(size_t __size);
void mm_free(mm_ptr<void> __ptr);

void mm_print_stat(FILE *__restrict stream);

class allocator {};
} // namespace mm_allocator

#endif // __MM_ALLOCATOR_H__