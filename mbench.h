#ifndef __MBENCH_H__
#define __MBENCH_H__

#include <atomic>
#include <numeric>
#include <pthread.h>
#include <thread>
#include <vector>

class mbench_base {
protected:
  virtual void test_env_init(int argc, char **argv) = 0;
  virtual void test_thread_env_init(int thread_id) = 0;
  virtual void test_thread_op(int thread_id, uint32_t n) = 0;
  virtual void test_phase_output(int thread_id, uint32_t n,
                                 uint64_t phase_time_us) = 0;
  virtual void test_thread_over(int thread_id, uint64_t use_time_us) = 0;
  virtual void test_over(uint64_t use_time_us) = 0;
  virtual void test_env_destory() = 0;

public:
  void run_test_task(int argc, char **argv) {
    test_env_init(argc, argv);

    std::vector<std::thread> ths;
    pthread_barrier_t barrier;
    std::atomic<int> output_time = {0};
    uint64_t test_start_time = UINT64_MAX;
    uint64_t test_end_time;
    pthread_barrier_init(
        &barrier, nullptr,
        std::accumulate(thread_count.begin(), thread_count.end(), 0));

    int thread_id = 0;
    for (auto n : thread_count) {
      if (iteration % n) {
        throw "need iteration % thread_cnt == 0";
      }

      for (int _tid = 0; _tid < n; ++thread_id, ++_tid) {
        ths.emplace_back([&, n, thread_id]() {
          uint32_t iteration_per_thread = iteration / n;
          uint32_t output_internal = iteration / output_internal_time;

          test_thread_env_init(thread_id);

          pthread_barrier_wait(&barrier);
          uint64_t start_us = get_wall_time();
          uint64_t last_us = start_us;
          test_start_time = std::min(test_start_time, start_us);
          for (uint32_t thread_iter = 0; thread_iter < iteration_per_thread;) {
            test_thread_op(thread_id,
                           (thread_id * iteration_per_thread) + thread_iter);

            if (++(thread_iter) % output_internal == 0) {
              uint32_t oiter =
                  (output_time.fetch_add(1, std::memory_order_relaxed) + 1) *
                  output_internal;

              uint64_t now_us = get_wall_time();
              test_phase_output(thread_id, oiter, now_us - last_us);
              last_us = now_us;
            }
          }
          uint64_t end_us = get_wall_time();
          test_end_time = end_us;

          test_thread_over(thread_id, end_us - start_us);
        });
      }
    }
    for (auto &th : ths) {
      th.join();
    }

    test_over(test_end_time - test_start_time);
    test_env_destory();
  }

protected:
  std::vector<int> thread_count;
  uint32_t iteration;
  int output_internal_time;

private:
  uint64_t get_wall_time() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  }
};

#endif // __MBENCH_H__
