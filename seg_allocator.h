#ifndef __SEG_ALLOCATOR_H__
#define __SEG_ALLOCATOR_H__

#include <algorithm>
#include <map>
#include <mutex>
#include <set>

struct seg_allocator_t {
  /**
   * @brief 区间集合
   * <size, offset>
   */
  std::mutex lock;
  std::set<std::pair<size_t, uint64_t>> free_seg_set_siz;
  std::map<uint64_t, size_t> free_seg_set_off;
  uint64_t base;
  size_t size;

  void init(uint64_t base, size_t size) {
    lock.lock();
    free_seg_set_siz.clear();
    free_seg_set_off.clear();
    free_seg_set_siz.insert(std::make_pair(size, base));
    free_seg_set_off.insert(std::make_pair(base, size));
    lock.unlock();
  }
  bool has_free_seg(size_t size) {
    auto iter = free_seg_set_siz.lower_bound(std::make_pair(size, 0UL));
    return iter != free_seg_set_siz.end();
  }
  uint64_t alloc(size_t size) {
    lock.lock();
    auto iter = free_seg_set_siz.lower_bound(std::make_pair(size, 0UL));
    if (iter == free_seg_set_siz.end()) {
      lock.unlock();
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
    lock.unlock();
    return off;
  }
  uint64_t alloc_placement(uint64_t off, size_t size) {
    lock.lock();
    auto iter =
        std::upper_bound(free_seg_set_off.rbegin(), free_seg_set_off.rend(),
                         off, [](const uint64_t a, std::pair<const uint64_t, size_t> b) { return a >= b.first; });
    if (iter == free_seg_set_off.rend()) {
      lock.unlock();
      return -1;
    }
    if (iter->first + iter->second < off + size) {
      lock.unlock();
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
      free_seg_set_off.insert(
          std::make_pair(off + size, less - iter->second));
      free_seg_set_siz.insert(
          std::make_pair(less - iter->second, off + size));
    }
    return off;
  }
  void free(uint64_t off, size_t size) {
    lock.lock();
    auto next = free_seg_set_off.lower_bound(off);
    int flag = 0;
    // check if merge next seg
    if (next != free_seg_set_off.end() && off + size == next->first) {
      flag |= 1;
    }

    auto iter = next--; // prev
    // check if merge prev seg
    if (next != free_seg_set_off.end() && next->first + next->second == off) {
      flag |= 2;
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
    lock.unlock();
  }
};
struct seg_allocator_greater_t : public seg_allocator_t {
  void init(uint64_t base, size_t size) {
    this->base = base;
    this->size = size;
    seg_allocator_t::init(base, size);
  }
  uint64_t alloc(size_t size) {
    uint64_t ret = seg_allocator_t::alloc(size);
    if (ret == -1)
      return -1;
    return base + this->size - 1 - ret + base - size;
  }
  uint64_t alloc_placement(uint64_t off, size_t size) {
    uint64_t p = base + this->size - 1 - off + base - size;
    uint64_t ret = seg_allocator_t::alloc_placement(p, size);
    if (ret == -1)
      return -1;
    return off;
  }
  void free(uint64_t off, size_t size) {
    uint64_t p = base + this->size - 1 - off + base - size;
    seg_allocator_t::free(p, size);
  }
};

#endif // __SEG_ALLOCATOR_H__