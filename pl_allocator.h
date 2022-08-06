#ifndef __PL_ALLOCATOR_H__
#define __PL_ALLOCATOR_H__

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>

template <typename _Tp, template <typename __Tp> class _Alloc = std::allocator>
class pl_allocator : public _Alloc<_Tp> {
  static thread_local std::vector<_Tp *> v_pl;
  static thread_local std::unordered_map<_Tp *, std::size_t> v_map;

public:
  _Tp *allocate(std::size_t n) {
    if (v_pl.empty() || n > 1) {
      _Tp *p = _Alloc<_Tp>::allocate(n);
      v_map.insert(std::make_pair(p, n));
      return p;
    }
    _Tp *p = v_pl.back();
    v_pl.pop_back();
    return p;
  }

  void deallocate(_Tp *p, std::size_t n) {
    if (n > 1) {
      v_pl.reserve(v_pl.size() + n);
      while (n != 0) {
        --n;
        v_pl.push_back(p + n);
      }
    }
    else {
      v_pl.push_back(p);
    }
  }

  bool has_thread_temp() {
    return !v_pl.empty();
  }

  void deallocate_thread_temp() {
    std::sort(v_pl.begin(), v_pl.end());
    for (std::size_t i = 0; i < v_pl.size(); ++i) {
      auto it = v_map.find(v_pl[i]);
      if (it != v_map.end()) {
        _Alloc<_Tp>::deallocate(v_pl[i], it->second);
        i += it->second - 1;
      } else {
        _Alloc<_Tp>::deallocate(v_pl[i], 1);
      }
    }
  }
};

template <typename _Tp, template <typename __Tp> class _Alloc>
thread_local std::vector<_Tp *> pl_allocator<_Tp, _Alloc>::v_pl;
template <typename _Tp, template <typename __Tp> class _Alloc>
thread_local std::unordered_map<_Tp *, std::size_t>
    pl_allocator<_Tp, _Alloc>::v_map;

#endif // __PL_ALLOCATOR_H__