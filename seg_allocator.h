#ifndef __SEG_ALLOCATOR_H__
#define __SEG_ALLOCATOR_H__

#include <algorithm>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>

class SegAllocator {
public:
  SegAllocator(void *base, size_t size)
      : base(reinterpret_cast<uintptr_t>(base)), size(size) {
    free_seg_set_siz.clear();
    free_seg_set_off.clear();
    free_seg_set_siz.insert(std::make_pair(size, this->base));
    free_seg_set_off.insert(std::make_pair(this->base, size));
  }

  void *allocate(size_t size) {
    auto iter = free_seg_set_siz.lower_bound(std::make_pair(size, 0UL));
    if (iter == free_seg_set_siz.end()) {
      return nullptr;
    }
    uintptr_t off = iter->second;
    size_t less = iter->first - size;
    if (less > 0) {
      free_seg_set_off.insert(std::make_pair(off + size, less));
      free_seg_set_siz.insert(std::make_pair(less, off + size));
    }
    free_seg_set_off.erase(iter->second);
    free_seg_set_siz.erase(iter);
    return reinterpret_cast<void *>(off);
  }

  void *placement_allocate(void *p, size_t size) {
    uintptr_t off = reinterpret_cast<uintptr_t>(p);

    auto iter = std::upper_bound(
        free_seg_set_off.rbegin(), free_seg_set_off.rend(), off,
        [](const uint64_t a, std::pair<const uint64_t, size_t> b) {
          return a >= b.first;
        });
    if (iter == free_seg_set_off.rend()) {
      return nullptr;
    }
    if (iter->first + iter->second < off + size) {
      return nullptr;
    }
    size_t less = iter->second - size;
    if (less == 0) {
      free_seg_set_off.erase(iter->first);
      free_seg_set_siz.erase(std::make_pair(iter->second, iter->first));
    } else if (iter->first == off) {
      free_seg_set_siz.erase(std::make_pair(iter->second, iter->first));
      free_seg_set_off.erase(iter->first);
      free_seg_set_off.insert(std::make_pair(off, less));
      free_seg_set_siz.insert(std::make_pair(less, off));
    } else if (iter->first + iter->second == off + size) {
      free_seg_set_siz.erase(std::make_pair(iter->second, iter->first));
      iter->second = less;
      free_seg_set_siz.insert(std::make_pair(iter->second, iter->first));
    } else {
      free_seg_set_siz.erase(std::make_pair(iter->second, iter->first));
      iter->second = off - iter->first;
      free_seg_set_siz.insert(std::make_pair(iter->second, iter->first));
      free_seg_set_off.insert(std::make_pair(off + size, less - iter->second));
      free_seg_set_siz.insert(std::make_pair(less - iter->second, off + size));
    }

    return p;
  }

  void deallocate(void *p, size_t size) {
    uintptr_t off = reinterpret_cast<uintptr_t>(p);

    auto next = free_seg_set_off.lower_bound(off), iter = next;
    int flag = 0;
    // check if merge next seg
    if (next != free_seg_set_off.end() && off + size == next->first) {
      flag |= 1;
    }

    if (next != free_seg_set_off.begin()) {
      --next; // prev
      // check if merge prev seg
      if (next != free_seg_set_off.end() && next->first + next->second == off) {
        flag |= 2;
      }
    }

    switch (flag) {
    case 0:
      free_seg_set_siz.insert(std::make_pair(size, off));
      free_seg_set_off.insert(std::make_pair(off, size));
      break;
    case 1:
      free_seg_set_siz.erase(std::make_pair(iter->second, iter->first));
      free_seg_set_siz.insert(
          std::make_pair(iter->second + size, iter->first - size));
      free_seg_set_off.insert(
          std::make_pair(iter->first - size, iter->second + size));
      free_seg_set_off.erase(iter);
      break;
    case 2:
      free_seg_set_siz.erase(std::make_pair(next->second, next->first));
      next->second += size;
      free_seg_set_siz.insert(std::make_pair(next->second, next->first));
      break;
    case 3:
      free_seg_set_siz.erase(std::make_pair(next->second, next->first));
      free_seg_set_siz.erase(std::make_pair(iter->second, iter->first));
      next->second += size + iter->second;
      free_seg_set_off.erase(iter);
      free_seg_set_siz.insert(std::make_pair(next->second, next->first));
      break;
    }
  }

protected:
  std::set<std::pair<size_t, uintptr_t>> free_seg_set_siz;
  std::map<uintptr_t, size_t> free_seg_set_off;
  uintptr_t base;
  size_t size;
};

class SegAllocatorReverse : public SegAllocator {
public:
  SegAllocatorReverse(void *base, size_t size) : SegAllocator(base, size) {}

  void *allocate(size_t size) {
    void *p = SegAllocator::allocate(size);
    if (p == nullptr)
      return nullptr;
    return reverse_ptr(p, size);
  }

  void *placement_allocate(void *p, size_t size) {
    void *rp = SegAllocator::placement_allocate(reverse_ptr(p, size), size);
    if (rp == nullptr)
      return nullptr;
    return p;
  }

  void deallocate(void *p, size_t size) {
    SegAllocator::deallocate(reverse_ptr(p, size), size);
  }

private:
  void *reverse_ptr(void *p, size_t size) {
    return reinterpret_cast<void *>(
        base + this->size - 1 - reinterpret_cast<uintptr_t>(p) + base - size);
  }
};

#endif // __SEG_ALLOCATOR_H__