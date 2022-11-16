#include "../conqueue.h"
#include "../dlog.h"
#include "../mbench.h"
#include "../statistic.h"
#include <bits/getopt_core.h>
#include <iostream>
#include <numeric>
#include <string>

using namespace std;

template <typename T> struct TestQueueModelBase {
  virtual ~TestQueueModelBase(){};
  virtual void enqueue(T x) = 0;
  virtual bool dequeue(T &x) = 0;
};

int prod_th_num, cons_th_num;

template <typename Q> struct queue_bench : public mbench_base {
  void test_env_init(int argc, char **argv) override {
    thread_count.push_back(prod_th_num);
    thread_count.push_back(cons_th_num);
    iteration = 200000;
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
  ConQ() : ConQueue<uint32_t>(262144) {}
  virtual ~ConQ() override {}
  virtual void enqueue(uint32_t x) override { ConQueue<uint32_t>::push(x); }
  virtual bool dequeue(uint32_t &x) override {
    return ConQueue<uint32_t>::pop(&x);
  }
};

struct CmQ : public TestQueueModelBase<uint32_t>, CmQueue<uint32_t> {
  CmQ() : CmQueue<uint32_t>(200000) {}
  virtual ~CmQ() override {}
  virtual void enqueue(uint32_t x) override {
    CmQueue<uint32_t>::push((uint32_t *)(size_t)x);
  }
  virtual bool dequeue(uint32_t &x) override {
    uint32_t *a;
    bool r = CmQueue<uint32_t>::pop(&a);
    x = (uintptr_t)a;
    return r;
  }
};

struct MpscQ : public TestQueueModelBase<uint32_t>,
               MpScNoRestrictBoundedQueue<uint32_t> {
  MpscQ() : MpScNoRestrictBoundedQueue<uint32_t>(262144) {}
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

int main() {
  {
    prod_th_num = 4;
    cons_th_num = 2;
    {
      queue_bench<ConQ> conq_bench;
      conq_bench.test_name = "ConQueue";
      conq_bench.run_test_task(0, nullptr);
    }

    {
      queue_bench<CmQ> cmq_bench;
      cmq_bench.test_name = "CmQueue";
      cmq_bench.run_test_task(0, nullptr);
    }
  }

  {
    prod_th_num = 4;
    cons_th_num = 1;
    {
      queue_bench<ConQ> conq_bench;
      conq_bench.test_name = "ConQueue";
      conq_bench.run_test_task(0, nullptr);
    }

    {
      queue_bench<CmQ> cmq_bench;
      cmq_bench.test_name = "CmQueue";
      cmq_bench.run_test_task(0, nullptr);
    }

    {
      queue_bench<MpscQ> mpscq_bench;
      mpscq_bench.test_name = "MpScNoRestrictBoundedQueue";
      mpscq_bench.run_test_task(0, nullptr);
    }
  }

  return 0;
}