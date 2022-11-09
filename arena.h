#ifndef __ARENA_H__
#define __ARENA_H__

#include "extend_mutex.h"
#include <cstdlib>
#include <deque>
#include <mutex>
#include <new>
#include <unordered_map>
#include <vector>

class Arena {
private:
  struct Block {
    uint32_t ref;
    bool allocing;
    char *base;
  };

  struct block_cursor_t {
    uint32_t m_bid = -1U;
    size_t m_pos = -1U;
  };
  struct thread_data {
    std::unordered_map<const Arena *, block_cursor_t> m_map_;

    static block_cursor_t &get_instance(const Arena *arena) {
      static thread_data tdata;
      return tdata.m_map_[arena];
    }
  };

  uint32_t get_ptr_to_bid(const char *ptr) const {
    return (ptr - memory_base()) / block_size;
  }

  uint32_t alloc_block() {
    uint32_t bid;
    {
      std::unique_lock<spin_mutex_b8> q_lock(m_lck);
      if (free_block_queue.empty())
        return -1U;
      bid = free_block_queue.back();
      free_block_queue.pop_back();
    }
    Block &m_base = blocks[bid];
    m_base.ref = 0;
    m_base.allocing = true;
    return bid;
  }

public:
  char *memory_base() const { return blocks[0].base; }

  Arena(char *mr, size_t nbytes) : nbytes(nbytes), is_self(false) {
    if (nbytes % block_size != 0)
      throw std::length_error(
          "`nbytes` needs to be an integer multiple of `b_size`");
    size_t bcount = (nbytes + block_size - 1) / block_size;
    blocks.resize(bcount);
    lcks = new spin_mutex_b8[bcount];
    for (size_t i = 0; i < bcount; ++i) {
      Block &b = blocks[i];
      b.ref = 0;
      b.base = mr + i * block_size;
      b.allocing = false;
    }
    // reduce cache miss at first time
    for (int j = 0; j < 8; ++j) {
      for (size_t i = 0; i < bcount / 8; ++i) {
        free_block_queue.push_back(i * 8 + j);
      }
    }
    size_t less = bcount - bcount / 8 * 8;
    for (size_t i = 0; i < less; ++i) {
      free_block_queue.push_back(i);
    }
  }

  Arena(size_t nbytes)
      : Arena(static_cast<char *>(::operator new(nbytes)), nbytes) {
    is_self = true;
  }

  ~Arena() {
    if (is_self)
      ::operator delete(memory_base());
    delete[] lcks;
  }

  char *allocate(size_t bytes) {
    auto &m_bid = thread_data::get_instance(this).m_bid;
    auto &m_pos = thread_data::get_instance(this).m_pos;

    char *ptr;
    if (m_pos + bytes > block_size || m_pos == -1UL) {
      if (m_bid != -1U) {
        Block &m_base = blocks[m_bid];
        std::unique_lock<spin_mutex_b8> b_lock(lcks[m_bid]);
        m_base.allocing = false;
        if (__atomic_load_n(&m_base.ref, __ATOMIC_RELAXED) != 0) {
          b_lock.unlock();
          m_bid = alloc_block();
        }
        // else reuse m_bid
      } else {
        m_bid = alloc_block();
      }
      m_pos = 0;
    }
    if (m_bid == -1U)
      return nullptr;
    Block &m_base = blocks[m_bid];
    ptr = m_base.base + m_pos;
    m_pos += bytes;
    __atomic_fetch_add(&m_base.ref, 1, __ATOMIC_RELAXED);
    return ptr;
  }

  void deallocate(const char *ptr) {
    if (ptr > memory_base() + nbytes || ptr < memory_base())
      throw std::out_of_range("out of range");

    uint32_t bid = get_ptr_to_bid(ptr);
    Block &m_base = blocks[bid];
    std::unique_lock<spin_mutex_b8> b_lock(lcks[bid]);
    uint32_t cur_ref = __atomic_sub_fetch(&m_base.ref, 1, __ATOMIC_RELAXED);
    if (!m_base.allocing && cur_ref == 0) {
      b_lock.unlock();
      std::unique_lock<spin_mutex_b8> q_lock(m_lck);
      free_block_queue.push_front(bid);
    }
  }

  static const size_t block_size = 4096;

private:
  bool is_self;
  size_t nbytes;
  spin_mutex_b8 m_lck;
  std::vector<Block> blocks;
  spin_mutex_b8 *lcks;
  std::deque<uint32_t> free_block_queue;
};

#endif // __ARENA_H__
