#ifndef __SEG_ALLOCATOR_H__
#define __SEG_ALLOCATOR_H__

#include <algorithm>
#include <map>
#include <mutex>
#include <set>

struct SegAllocator {
  std::mutex lock;
  std::set<std::pair<size_t, uint64_t>> free_seg_set_siz;
  std::map<uint64_t, size_t> free_seg_set_off;
  uint64_t base;

  void init(uint64_t base, size_t size) {
    free_seg_set_siz.clear();
    free_seg_set_off.clear();
    free_seg_set_siz.insert(std::make_pair(size, base));
    free_seg_set_off.insert(std::make_pair(base, size));
  }
  bool has_free_seg(size_t size) const {
    auto iter = free_seg_set_siz.lower_bound(std::make_pair(size, 0UL));
    return iter != free_seg_set_siz.end();
  }
  uint64_t allocate(size_t size) {
    std::unique_lock<std::mutex> lck(lock);
    auto iter = free_seg_set_siz.lower_bound(std::make_pair(size, 0UL));
    if (iter == free_seg_set_siz.end()) {
      return -1;
    }
    uint64_t off = iter->second;
    size_t less = iter->first - size;
    if (less > 0) {
      free_seg_set_off.insert(std::make_pair(off + size, less));
      free_seg_set_siz.insert(std::make_pair(less, off + size));
    }
    free_seg_set_off.erase(iter->second);
    free_seg_set_siz.erase(iter);
    return off;
  }
  uint64_t placement_allocate(uint64_t off, size_t size) {
    std::unique_lock<std::mutex> lck(lock);
    auto iter = std::upper_bound(
        free_seg_set_off.rbegin(), free_seg_set_off.rend(), off,
        [](const uint64_t a, std::pair<const uint64_t, size_t> b) {
          return a >= b.first;
        });
    if (iter == free_seg_set_off.rend()) {
      return -1;
    }
    if (iter->first + iter->second < off + size) {
      return -1;
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
    return off;
  }
  void deallocate(uint64_t off, size_t size) {
    std::unique_lock<std::mutex> lck(lock);
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
};
struct SegAllocatorGreater : public SegAllocator {
  size_t size;

  void init(uint64_t base, size_t size) {
    this->base = base;
    this->size = size;
    SegAllocator::init(base, size);
  }
  uint64_t allocate(size_t size) {
    uint64_t ret = SegAllocator::allocate(size);
    if (ret == -1)
      return -1;
    return base + this->size - 1 - ret + base - size;
  }
  uint64_t placement_allocate(uint64_t off, size_t size) {
    uint64_t p = base + this->size - 1 - off + base - size;
    uint64_t ret = SegAllocator::placement_allocate(p, size);
    if (ret == -1)
      return -1;
    return off;
  }
  void deallocate(uint64_t off, size_t size) {
    uint64_t p = base + this->size - 1 - off + base - size;
    SegAllocator::deallocate(p, size);
  }
};

#endif // __SEG_ALLOCATOR_H__