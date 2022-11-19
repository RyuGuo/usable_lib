#include "../conqueue.h"
#include "../dlog.h"
#include "../mbench.h"
#include <iostream>
#include <string>

using namespace std;

constexpr uint32_t upper_power_of_2(uint32_t n) {
  --n;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  return n + 1;
}

const uint32_t IT = 1000000;
int prod_th_num, cons_th_num;
int _ts_ = 0; // 0: hts, 1: rts

uint32_t conq_flag_get() {
  uint32_t flag = 0;
  if (prod_th_num == 1) {
    flag |= ConQueueMode::F_SP_ENQ;
  } else if (_ts_ == 0) {
    flag |= ConQueueMode::F_MP_HTS_ENQ;
  } else {
    flag |= ConQueueMode::F_MP_RTS_ENQ;
  }
  if (cons_th_num == 1) {
    flag |= ConQueueMode::F_SC_DEQ;
  } else if (_ts_ == 0) {
    flag |= ConQueueMode::F_MC_HTS_DEQ;
  } else {
    flag |= ConQueueMode::F_MC_RTS_DEQ;
  }
  return flag;
}

template <typename T> struct TestQueueModelBase {
  virtual ~TestQueueModelBase(){};
  virtual void enqueue(T x) = 0;
  virtual bool dequeue(T &x) = 0;
};

template <typename Q> struct queue_bench : public mbench_base {
  void test_env_init(int argc, char **argv) override {
    thread_count.push_back(prod_th_num);
    thread_count.push_back(cons_th_num);
    iteration = IT;
    output_internal_time = 1;
    s.resize(prod_th_num + cons_th_num);

    cout << test_name << " test start (prod: " << prod_th_num
         << ", cons: " << cons_th_num << ")" << endl;
  }
  void test_thread_env_init(int thread_id) override {}
  void test_thread_op(int thread_id, uint32_t n) override {
    if (thread_id < prod_th_num) {
      q.enqueue(n);
    } else {
      uint32_t r = 0;
      while (!q.dequeue(r)) {
      }
      s[thread_id].c += r;
    }
  }
  void test_phase_output(int thread_id, uint32_t n,
                         uint64_t phase_time_us) override {}
  void test_thread_over(int thread_id, uint64_t use_time_us) override {}
  void test_over(uint64_t use_time_us) override {
    long S = std::accumulate(s.begin(), s.end(), (long)0,
                             [](long pre, W &w) { return pre + w.c; });
    DLOG_EXPR(S, ==, (long)iteration * (iteration - 1) / 2);
    cout << "Avg Throughputs: " << (1.0 * iteration / use_time_us) << "Mops"
         << endl;
  }
  void test_env_destory() override {}

  Q q;
  std::string test_name;
  struct W {
    long c = 0;
    int __padding__[14];
  };
  std::vector<W> s;
};

struct ConQ : public TestQueueModelBase<uint32_t>, ConQueue<uint32_t> {
  ConQ() : ConQueue<uint32_t>(upper_power_of_2(IT), conq_flag_get()) {}
  virtual ~ConQ() override {}
  virtual void enqueue(uint32_t x) override { ConQueue<uint32_t>::push(x); }
  virtual bool dequeue(uint32_t &x) override {
    return ConQueue<uint32_t>::pop(&x);
  }
};

struct CLQ : public TestQueueModelBase<uint32_t>, CLQueue<uint32_t> {
  CLQ() : CLQueue<uint32_t>(upper_power_of_2(IT), conq_flag_get()) {}
  virtual ~CLQ() override {}
  virtual void enqueue(uint32_t x) override {
    CLQueue<uint32_t>::push_with_delay(x);
  }
  virtual bool dequeue(uint32_t &x) override {
    return CLQueue<uint32_t>::pop(&x);
  }
};

struct MpscQ : public TestQueueModelBase<uint32_t>,
               MpScNoRestrictBoundedQueue<uint32_t> {
  MpscQ() : MpScNoRestrictBoundedQueue<uint32_t>(upper_power_of_2(IT)) {}
  virtual ~MpscQ() override {}
  virtual void enqueue(uint32_t x) override {
    MpScNoRestrictBoundedQueue<uint32_t>::push(x);
  }
  virtual bool dequeue(uint32_t &x) override {
    if (MpScNoRestrictBoundedQueue<uint32_t>::empty()) {
      return false;
    }
    return MpScNoRestrictBoundedQueue<uint32_t>::pop(&x);
  }
};

#include "rte_ring.h"

struct RteQ : public TestQueueModelBase<uint32_t>, RteRing<uint32_t> {
  RteQ()
      : RteRing<uint32_t>(upper_power_of_2(IT), prod_th_num == 1,
                          cons_th_num == 1) {}
  virtual ~RteQ() override {}
  virtual void enqueue(uint32_t x) override {
    RteRing<uint32_t>::enqueue((uint32_t *)(size_t)x);
  }
  virtual bool dequeue(uint32_t &x) override {
    uint32_t *a;
    bool r = RteRing<uint32_t>::dequeue(&a) == RTE_RING_OK;
    x = (uintptr_t)a;
    return r;
  }
};

#include "atomic_queue.h"

struct AtQ : public TestQueueModelBase<uint32_t>,
             atomic_queue::RetryDecorator<atomic_queue::AtomicQueue<
                 uint32_t, upper_power_of_2(IT), uint32_t(-1ul)>> {
  using Q = atomic_queue::RetryDecorator<atomic_queue::AtomicQueue<
      uint32_t, upper_power_of_2(IT), uint32_t(-1ul)>>;

  AtQ() : Q() {}
  virtual ~AtQ() override {}
  virtual void enqueue(uint32_t x) override { Q::push(x); }
  virtual bool dequeue(uint32_t &x) override {
    x = Q::pop();
    return true;
  }
};

#include "moodycamel.h"

struct ModcQ : public TestQueueModelBase<uint32_t>,
               moodycamel::ConcurrentQueue<uint32_t> {
  using Q = moodycamel::ConcurrentQueue<uint32_t>;

  ModcQ() : Q() {}
  virtual ~ModcQ() override {}
  virtual void enqueue(uint32_t x) override { Q::enqueue(x); }
  virtual bool dequeue(uint32_t &x) override { return Q::try_dequeue(x); }
};

int main() {
  std::vector<int> v = {1, 2, 4, 5, 8, 10, 16, 20};

  for (int c = 0; c < v.size(); ++c) {
    for (int p = 0; p < v.size(); ++p) {
      {
        _ts_ = 0;
        prod_th_num = v[p];
        cons_th_num = v[c];
        {
          queue_bench<RteQ> rteq_bench;
          rteq_bench.test_name = "RteRing";
          rteq_bench.run_test_task(0, nullptr);
        }

        if (_ts_ == 1) {
          // AtomicQueue is optimism choice, same as rts mode in rte_ring
          queue_bench<AtQ> atq_bench;
          atq_bench.test_name = "AtomicQueue";
          atq_bench.run_test_task(0, nullptr);
        }

        if (_ts_ == 1) {
          // moodycamel is not linearizable, same as rts mode in rte_ring
          queue_bench<ModcQ> modcq_bench;
          modcq_bench.test_name = "moodycamel";
          modcq_bench.run_test_task(0, nullptr);
        }

        {
          queue_bench<ConQ> conq_bench;
          conq_bench.test_name = "ConQueue";
          conq_bench.run_test_task(0, nullptr);
        }

        {
          queue_bench<CLQ> clq_bench;
          clq_bench.test_name = "CLQueue";
          clq_bench.run_test_task(0, nullptr);
        }

        if (cons_th_num == 1) {
          queue_bench<MpscQ> mpscq_bench;
          mpscq_bench.test_name = "MpScNoRestrictBoundedQueue";
          mpscq_bench.run_test_task(0, nullptr);
        }
      }
    }
  }

  return 0;
}