#ifndef __PL_ALLOCATOR_H__
#define __PL_ALLOCATOR_H__

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <vector>

template <typename _Tp, template <typename __Tp> class _Alloc = std::allocator>
class pl_allocator : public _Alloc<_Tp> {
  struct pl_thread_data {
    std::vector<_Tp *> v_pl;
    std::unordered_map<_Tp *, std::size_t> v_map;

    ~pl_thread_data() {
      std::sort(v_pl.begin(), v_pl.end());
      for (std::size_t i = 0; i < v_pl.size(); ++i) {
        auto it = v_map.find(v_pl[i]);
        if (it != v_map.end()) {
          _Alloc<_Tp>().deallocate(v_pl[i], it->second);
          i += it->second - 1;
          v_map.erase(it);
        } else {
          _Alloc<_Tp>().deallocate(v_pl[i], 1);
        }
      }
      if (!v_map.empty()) {
        perror("pl allocator memory leak");
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
    auto &v_map = pl_thread_data::get_instance().v_map;

    _Tp *p;
    if (v_pl.empty()) {
      p = _Alloc<_Tp>::allocate(n);
      if (n > 1) {
        v_map.insert(std::make_pair(p, n));
      }
    } else {
      p = v_pl.back();
      v_pl.pop_back();
    }
    return p;
  }

  void deallocate(_Tp *p, std::size_t n) {
    auto &v_pl = pl_thread_data::get_instance().v_pl;
    auto &v_map = pl_thread_data::get_instance().v_map;

    if (n > 1) {
      v_pl.reserve(v_pl.size() + n);
      while (n != 0) {
        --n;
        v_pl.push_back(p + n);
      }
    } else {
      v_pl.push_back(p);
    }
  }
};

#endif // __PL_ALLOCATOR_H__