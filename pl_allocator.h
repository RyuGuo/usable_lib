#ifndef __PL_ALLOCATOR_H__
#define __PL_ALLOCATOR_H__

#include <memory>
#include <vector>

template <typename _Tp, template <typename __Tp> class _Alloc = std::allocator>
class pl_allocator : public _Alloc<_Tp> {
  static thread_local std::vector<_Tp *> v_pl;

public:
  _Tp *allocate(std::size_t n) {
    if (v_pl.empty() || n > 1)
      return _Alloc<_Tp>::allocate(n);
    _Tp *p = v_pl.back();
    v_pl.pop_back();
    return p;
  }

  void deallocate(_Tp *p, std::size_t n) {
    if (n > 1)
      _Alloc<_Tp>::deallocate(p, n);
    else
      v_pl.push_back(p);
  }

  void clear_thread_temp() {
    for (auto &e : v_pl)
      _Alloc<_Tp>::deallocate(e, 1);
  }
};

template <typename _Tp, template <typename __Tp> class _Alloc>
thread_local std::vector<_Tp *> pl_allocator<_Tp, _Alloc>::v_pl;

#endif // __PL_ALLOCATOR_H__