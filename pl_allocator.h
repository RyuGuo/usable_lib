#ifndef __PL_ALLOCATOR_H__
#define __PL_ALLOCATOR_H__

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>

template <typename _Tp, typename _Alloc = std::allocator<_Tp>>
class pl_allocator : public _Alloc {
  struct pl_thread_data {
    std::vector<_Tp *> v_pl;

    ~pl_thread_data() {
      for (auto p : v_pl) {
        _Alloc().deallocate(p, 1);
      }
    }

    static pl_thread_data &get_instance() {
      static thread_local pl_thread_data tdata;
      return tdata;
    }
  };

public:
  _Tp *allocate(std::size_t n) {
    auto &v_pl = pl_thread_data::get_instance().v_pl;

    _Tp *p;

    if (n > 1 || v_pl.empty()) {
      p = _Alloc().allocate(n);
    } else {
      p = v_pl.back();
      v_pl.pop_back();
    }

    return p;
  }

  void deallocate(_Tp *p, std::size_t n) {
    auto &v_pl = pl_thread_data::get_instance().v_pl;

    if (n > 1) {
      _Alloc().deallocate(p, n);
    } else {
      v_pl.push_back(p);
    }
  }
};

#endif // __PL_ALLOCATOR_H__