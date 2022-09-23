#include <cstddef>
#include <utility>

#define flexible_array(T, ft_name)                                             \
  __flexible_array_impl__<                                                     \
      T, typename std::remove_reference<decltype(*T::ft_name)>::type,          \
      offsetof(T, ft_name)>

template <typename T, typename FT, size_t FT_OFFSET>
struct __flexible_array_impl__ final : public T {
  template <typename... Args> void *operator new(size_t, size_t n) {
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
