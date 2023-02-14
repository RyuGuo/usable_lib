#ifndef _RTE_RING_H_
#define _RTE_RING_H_

#include <cassert>
#include <cstdint>
#include <thread>

struct RteRingMode {
  static constexpr int SPSC = 0;
  static constexpr int MP = 1;
  static constexpr int MC = 2;
  static constexpr int MPMC = MP | MC;
};

template <typename T, int mode = RteRingMode::SPSC, int size = 4096>
class RteRing {
  inline static void cpu_relax() { asm volatile("pause\n" : : : "memory"); }

public:
  static constexpr unsigned RTX_RTE_RING_REP_COUNT = 3;

  enum Status {
    RTE_RING_OK = 0,
    RTX_RING_FULL,
    RTX_RING_NOT_ENOUGH,
  };

  /**
   * @brief ruct a new Rte Ring object
   */
  RteRing() {
    // verify size is 2^x
    static_assert((size & (-size)) == size, "size must be 2^x");
    prod.prod_tail_ = prod.prod_head_ = 0;
    cons.cons_tail_ = cons.cons_head_ = 0;
    objs_ = new T *[size];
  }

  ~RteRing() { delete[] objs_; }

  Status enqueue(T *obj_table) {
    if (mode & RteRingMode::MP)
      return enqueue_mp(obj_table);
    else
      return enqueue_sp(obj_table);
  }

  Status dequeue(T **obj_table) {
    if (mode & RteRingMode::MC)
      return dequeue_mc(obj_table);
    else
      return dequeue_sc(obj_table);
  }

  Status enqueue(uint32_t n, T **obj_table, uint32_t *res_n = nullptr,
                 bool fixed = false) {
    if (mode & RteRingMode::MP)
      return enqueue_mp_bulk(n, obj_table, res_n, fixed);
    else
      return enqueue_sp_bulk(n, obj_table, res_n, fixed);
  }

  Status dequeue(uint32_t n, T **obj_table, uint32_t *res_n = nullptr,
                 bool fixed = false) {
    if (mode & RteRingMode::MC)
      return dequeue_mc_bulk(n, obj_table, res_n, fixed);
    else
      return dequeue_sc_bulk(n, obj_table, res_n, fixed);
  }

private:
  Status enqueue_sp(T *obj_table) {
    uint32_t prod_head, cons_tail;
    uint32_t prod_next, free_entries;

    prod_head = prod.prod_head_;
    cons_tail = cons.cons_tail_;
    // 即使 prod_head 与 cons_tail 溢出 u32 也不会出错
    free_entries = mask + cons_tail - prod_head;

    if (free_entries == 0) {
      return RTX_RING_FULL;
    }

    prod_next = prod_head + 1;
    prod.prod_head_ = prod_next;

    objs_[(prod_head)&mask] = obj_table;

    prod.prod_tail_ = prod_next;
    return RTE_RING_OK;
  }

  Status enqueue_mp(T *obj_table) {
    uint32_t prod_head, cons_tail;
    uint32_t prod_next, free_entries;
    unsigned rep = 0;

    do {
      prod_head = prod.prod_head_;
      cons_tail = cons.cons_tail_;
      free_entries = mask + cons_tail - prod_head;

      if (free_entries == 0) {
        return RTX_RING_FULL;
      }

      prod_next = prod_head + 1;
      if (__atomic_compare_exchange_n(&prod.prod_head_, &prod_head, prod_next,
                                      false, __ATOMIC_ACQ_REL,
                                      __ATOMIC_RELAXED)) {
        break;
      }
    } while (true);

    objs_[(prod_head)&mask] = obj_table;

    while (prod.prod_tail_ != prod_head) {
      cpu_relax();

      if (++rep == RTX_RTE_RING_REP_COUNT) {
        rep = 0;
        sched_yield();
      }
    }
    prod.prod_tail_ = prod_next;
    return RTE_RING_OK;
  }

  Status enqueue_sp_bulk(uint32_t n, T **obj_table, uint32_t *res_n,
                         bool fixed) {
    uint32_t prod_head, cons_tail;
    uint32_t prod_next, free_entries;

    prod_head = prod.prod_head_;
    cons_tail = cons.cons_tail_;
    // 即使 prod_head 与 cons_tail 溢出 u32 也不会出错
    free_entries = mask + cons_tail - prod_head;

    if (n > free_entries) {
      if (fixed || free_entries == 0)
        return RTX_RING_FULL;
      n = free_entries;
    }

    prod_next = prod_head + n;
    prod.prod_head_ = prod_next;

    for (uint32_t i = 0; i < n; ++i) {
      objs_[(prod_head + i) & mask] = obj_table[i];
    }

    prod.prod_tail_ = prod_next;
    if (res_n)
      *res_n = n;
    return RTE_RING_OK;
  }

  Status enqueue_mp_bulk(uint32_t n, T **obj_table, uint32_t *res_n,
                         bool fixed) {
    uint32_t prod_head, cons_tail;
    uint32_t prod_next, free_entries;
    uint32_t max_n = n;
    unsigned rep = 0;

    do {
      n = max_n;
      prod_head = prod.prod_head_;
      cons_tail = cons.cons_tail_;
      free_entries = mask + cons_tail - prod_head;

      if (n > free_entries) {
        if (fixed || free_entries == 0)
          return RTX_RING_FULL;
        n = free_entries;
      }

      prod_next = prod_head + n;
      // printf("prod.prod_head_: %u prod_head: %u, prod_next: %u\n",
      //         prod.prod_head_, prod_head, prod_next);
      if (__atomic_compare_exchange_n(&prod.prod_head_, &prod_head, prod_next,
                                      false, __ATOMIC_ACQ_REL,
                                      __ATOMIC_RELAXED)) {
        break;
      }
    } while (true);

    for (uint32_t i = 0; i < n; ++i) {
      objs_[(prod_head + i) & mask] = obj_table[i];
    }

    while (prod.prod_tail_ != prod_head) {
      cpu_relax();

      if (++rep == RTX_RTE_RING_REP_COUNT) {
        rep = 0;
        sched_yield();
      }
    }
    prod.prod_tail_ = prod_next;
    if (res_n)
      *res_n = n;
    return RTE_RING_OK;
  }

  Status dequeue_sc(T **obj_table) {
    uint32_t cons_head, prod_tail;
    uint32_t cons_next, entries;

    cons_head = cons.cons_head_;
    prod_tail = prod.prod_tail_;
    entries = prod_tail - cons_head;

    if (entries == 0) {
      return RTX_RING_NOT_ENOUGH;
    }

    cons_next = cons_head + 1;
    cons.cons_head_ = cons_next;

    *obj_table = objs_[(cons_head) % mask];

    cons.cons_tail_ = cons_next;
    return RTE_RING_OK;
  }

  Status dequeue_mc(T **obj_table) {
    uint32_t cons_head, prod_tail;
    uint32_t cons_next, entries;
    unsigned rep = 0;

    do {
      cons_head = cons.cons_head_;
      prod_tail = prod.prod_tail_;
      entries = prod_tail - cons_head;

      if (entries == 0) {
        return RTX_RING_NOT_ENOUGH;
      }

      cons_next = cons_head + 1;
      // printf("cons.cons_head_: %u cons_head: %u, cons_next: %u\n",
      //         cons.cons_head_, cons_head, cons_next);
      if (__atomic_compare_exchange_n(&cons.cons_head_, &cons_head, cons_next,
                                      false, __ATOMIC_ACQ_REL,
                                      __ATOMIC_RELAXED)) {
        break;
      }
    } while (true);

    *obj_table = objs_[(cons_head) % mask];

    while (cons.cons_tail_ != cons_head) {
      cpu_relax();

      if (++rep == RTX_RTE_RING_REP_COUNT) {
        rep = 0;
        sched_yield();
      }
    }
    cons.cons_tail_ = cons_next;
    return RTE_RING_OK;
  }

  Status dequeue_sc_bulk(uint32_t n, T **obj_table, uint32_t *res_n,
                         bool fixed) {
    uint32_t cons_head, prod_tail;
    uint32_t cons_next, entries;

    cons_head = cons.cons_head_;
    prod_tail = prod.prod_tail_;
    entries = prod_tail - cons_head;

    res_n = 0;
    if (n > entries) {
      if (fixed || entries == 0)
        return RTX_RING_NOT_ENOUGH;
      n = entries;
    }

    cons_next = cons_head + n;
    cons.cons_head_ = cons_next;

    for (uint32_t i = 0; i < n; ++i) {
      obj_table[i] = objs_[(cons_head + i) % mask];
    }

    cons.cons_tail_ = cons_next;
    if (res_n)
      *res_n = n;
    return RTE_RING_OK;
  }

  Status dequeue_mc_bulk(uint32_t n, T **obj_table, uint32_t *res_n,
                         bool fixed) {
    uint32_t cons_head, prod_tail;
    uint32_t cons_next, entries;
    uint32_t max_n = n;
    unsigned rep = 0;

    res_n = 0;
    do {
      n = max_n;

      cons_head = cons.cons_head_;
      prod_tail = prod.prod_tail_;
      entries = prod_tail - cons_head;

      if (n > entries) {
        if (fixed || entries == 0)
          return RTX_RING_NOT_ENOUGH;
        n = entries;
      }

      cons_next = cons_head + n;
      // printf("cons.cons_head_: %u cons_head: %u, cons_next: %u\n",
      //         cons.cons_head_, cons_head, cons_next);
      if (__atomic_compare_exchange_n(&cons.cons_head_, &cons_head, cons_next,
                                      false, __ATOMIC_ACQ_REL,
                                      __ATOMIC_RELAXED)) {
        break;
      }
    } while (true);

    for (uint32_t i = 0; i < n; ++i) {
      obj_table[i] = objs_[(cons_head + i) % mask];
    }

    while (cons.cons_tail_ != cons_head) {
      cpu_relax();

      if (++rep == RTX_RTE_RING_REP_COUNT) {
        rep = 0;
        sched_yield();
      }
    }
    cons.cons_tail_ = cons_next;
    if (res_n)
      *res_n = n;
    return RTE_RING_OK;
  }

  struct Prod {
    volatile uint32_t prod_head_;
    volatile uint32_t prod_tail_;
  } prod __attribute__((aligned(64)));

  struct Cons {
    volatile uint32_t cons_head_;
    volatile uint32_t cons_tail_;
  } cons __attribute__((aligned(64)));

  T **objs_;
  const uint32_t mask = size - 1;
};

#endif // _RTE_RING_H_