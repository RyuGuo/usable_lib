#include "../conqueue.h"
#include "../csv.h"
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
int _ts_ = 0; // 0: default, 1: rts

uint32_t conq_flag_get() {
  uint32_t flag = 0;
  if (prod_th_num == 1) {
    flag |= ConQueueMode::F_SP_ENQ;
  } else if (_ts_ == 0) {
    flag |= ConQueueMode::F_MP_ENQ;
  } else {
    flag |= ConQueueMode::F_MP_RTS_ENQ;
  }
  if (cons_th_num == 1) {
    flag |= ConQueueMode::F_SC_DEQ;
  } else if (_ts_ == 0) {
    flag |= ConQueueMode::F_MC_DEQ;
  } else {
    flag |= ConQueueMode::F_MC_RTS_DEQ;
  }
  return flag;
}

template <typename T> struct TestQueueModelBase {
  virtual ~TestQueueModelBase(){};
  virtual bool enqueue(T x) = 0;
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
      while (!q.enqueue(n))
        ;
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
    result_throughput = 1.0 * iteration / use_time_us;
    cout << "Avg Throughputs: " << result_throughput << "Mops" << endl;
  }
  void test_env_destory() override {}

  Q q;
  std::string test_name;
  struct W {
    long c = 0;
    int __padding__[14];
  };
  std::vector<W> s;
  double result_throughput;
};

struct ConQ : public TestQueueModelBase<uint32_t>, ConQueue<uint32_t> {
  ConQ() : ConQueue<uint32_t>(upper_power_of_2(IT), conq_flag_get()) {}
  virtual ~ConQ() override {}
  virtual bool enqueue(uint32_t x) override {
    return ConQueue<uint32_t>::push(x);
  }
  virtual bool dequeue(uint32_t &x) override {
    return ConQueue<uint32_t>::pop(&x);
  }
};

struct CLQ : public TestQueueModelBase<uint32_t>, CLQueue<uint32_t> {
  CLQ() : CLQueue<uint32_t>(upper_power_of_2(IT), conq_flag_get()) {}
  virtual ~CLQ() override {}
  virtual bool enqueue(uint32_t x) override {
    return CLQueue<uint32_t>::push(x);
  }
  virtual bool dequeue(uint32_t &x) override {
    return CLQueue<uint32_t>::pop(&x);
  }
};

#include "rte_ring.h"

template <int mode = RteRingMode::SPSC>
struct RteQ : public TestQueueModelBase<uint32_t>,
              RteRing<uint32_t, mode, upper_power_of_2(IT)> {
  using RTE_RING = RteRing<uint32_t, mode, upper_power_of_2(IT)>;
  RteQ() : RTE_RING() {}
  virtual ~RteQ() override {}
  virtual bool enqueue(uint32_t x) override {
    return RTE_RING::enqueue((uint32_t *)(size_t)x) == RTE_RING::RTE_RING_OK;
  }
  virtual bool dequeue(uint32_t &x) override {
    uint32_t *a;
    bool r = RTE_RING::dequeue(&a) == RTE_RING::RTE_RING_OK;
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
  virtual bool enqueue(uint32_t x) override {
    Q::push(x);
    return true;
  }
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
  virtual bool enqueue(uint32_t x) override { return Q::enqueue(x); }
  virtual bool dequeue(uint32_t &x) override { return Q::try_dequeue(x); }
};

int main() {
  std::vector<int> v = {1, 2, 4, 5, 8, 10, 16, 20};
  CSVFileStream *csv_p;
  _ts_ = 0;

  for (int c = 0; c < v.size(); ++c) {
    cons_th_num = v[c];
    if (_ts_ == 0) {
      csv_p = new CSVFileStream("out_c_" + to_string(cons_th_num) + ".csv",
                                {"prod", "rte_ring", "clqueue"});
    } else {
      csv_p = new CSVFileStream(
          "out_c_" + to_string(cons_th_num) + ".csv",
          {"prod", "rte_ring", "atomic_queue", "moodycamel", "clqueue"});
    }
    for (int p = 0; p < v.size(); ++p) {
      prod_th_num = v[p];

      double rte_ring_throughput, atomic_queue_throughput,
          moodycamel_throughput, conqueue_throughput, clqueue_throughput;

      {
        if (cons_th_num == 1 && prod_th_num == 1) {
          queue_bench<RteQ<>> rteq_bench;
          rteq_bench.test_name = "RteRing SPSC";
          rteq_bench.run_test_task(0, nullptr);
          rte_ring_throughput = rteq_bench.result_throughput;
        } else if (cons_th_num > 1 && prod_th_num == 1) {
          queue_bench<RteQ<RteRingMode::MC>> rteq_bench;
          rteq_bench.test_name = "RteRing SPMC";
          rteq_bench.run_test_task(0, nullptr);
          rte_ring_throughput = rteq_bench.result_throughput;
        } else if (cons_th_num == 1 && prod_th_num > 1) {
          queue_bench<RteQ<RteRingMode::MP>> rteq_bench;
          rteq_bench.test_name = "RteRing MPSC";
          rteq_bench.run_test_task(0, nullptr);
          rte_ring_throughput = rteq_bench.result_throughput;
        } else {
          queue_bench<RteQ<RteRingMode::MPMC>> rteq_bench;
          rteq_bench.test_name = "RteRing MPMC";
          rteq_bench.run_test_task(0, nullptr);
          rte_ring_throughput = rteq_bench.result_throughput;
        }
      }

      if (_ts_ == 1) {
        // AtomicQueue is optimism choice, same as rts mode in rte_ring
        queue_bench<AtQ> atq_bench;
        atq_bench.test_name = "AtomicQueue";
        atq_bench.run_test_task(0, nullptr);
        atomic_queue_throughput = atq_bench.result_throughput;
      }

      if (_ts_ == 1) {
        // moodycamel is not linearizable, same as rts mode in rte_ring
        queue_bench<ModcQ> modcq_bench;
        modcq_bench.test_name = "moodycamel";
        modcq_bench.run_test_task(0, nullptr);
        moodycamel_throughput = modcq_bench.result_throughput;
      }

      {
        queue_bench<ConQ> conq_bench;
        conq_bench.test_name = "ConQueue";
        conq_bench.run_test_task(0, nullptr);
        conqueue_throughput = conq_bench.result_throughput;
      }

      {
        queue_bench<CLQ> clq_bench;
        clq_bench.test_name = "CLQueue";
        clq_bench.run_test_task(0, nullptr);
        clqueue_throughput = clq_bench.result_throughput;
      }

      printf("===================================\n");

      if (_ts_ == 0) {
        csv_p->appendEntry(prod_th_num, rte_ring_throughput,
                           clqueue_throughput);
      } else {
        csv_p->appendEntry(prod_th_num, rte_ring_throughput,
                           atomic_queue_throughput, moodycamel_throughput,
                           clqueue_throughput);
      }
    }

    delete csv_p;
  }

  return 0;
}