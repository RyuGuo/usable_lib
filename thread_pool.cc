#ifndef __THREAD_POOL_H__
#define __THREAD_POOL_H__

#include "pl_allocator.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <random>
#include <string.h>
#include <thread>
#include <vector>

class ThreadPool {
private:
  template <typename _Signature>
  struct PTask : public std::packaged_task<_Signature> {
    template <typename _Fn>
    PTask(_Fn &&__fn)
        : std::packaged_task<_Signature>(std::forward<_Fn>(__fn)) {}

    void *operator new(size_t size) {
      return pl_allocator<std::packaged_task<_Signature>>().allocate(
          size / sizeof(std::packaged_task<_Signature>));
    }
    void operator delete(void *p, size_t size) {
      pl_allocator<std::packaged_task<_Signature>>().deallocate(
          static_cast<std::packaged_task<_Signature> *>(p),
          size / sizeof(std::packaged_task<_Signature>));
    }
  };

  struct W {
    std::mutex mlck;
    std::condition_variable cv;
    std::thread worker_thread;
  };

  struct Worker {
    Worker(uint8_t tid, ThreadPool *pool) : tid(tid), pool(pool), stop(false) {
      w_ = new W();
      w_->worker_thread = std::thread(&Worker::routine, this);
    }
    ~Worker() {
      {
        std::unique_lock<std::mutex> lock(w_->mlck);
        stop = true;
      }
      w_->cv.notify_one();
      w_->worker_thread.join();
      delete w_;
    }

    void routine() {
      while (1) {
        bool pool_pop = false;
        std::function<void()> task;

        {
          std::unique_lock<std::mutex> lock(w_->mlck);
          if (tasks.empty()) {
            pool->busy_bits.fetch_and(~(1UL << tid), std::memory_order_relaxed);
            w_->cv.wait(lock, [this] { return stop || !tasks.empty(); });
            if (stop && tasks.empty())
              return;
            pool->busy_bits.fetch_or(1UL << tid, std::memory_order_relaxed);
          }
          task = std::move(tasks.front());
          tasks.pop();
        }

        task();
      }
    }

    bool stop;
    uint8_t tid;
    ThreadPool *pool;
    W *w_;
    std::queue<std::function<void()>> tasks;
  };

  static uint8_t min(const uint8_t a, const uint8_t b) {
    return std::min(a, b);
  }

public:
  ThreadPool(uint8_t threads = 1) : busy_bits(0), rng(time(nullptr)) {
    workers.reserve(max_thread_count);
    add_threads(threads);
  }

  uint8_t count() { return workers.size(); }

  void add_threads(uint8_t threads) {
    threads = min(threads, max_thread_count - count());
    for (uint8_t i = 0; i < threads; ++i)
      workers.emplace_back(workers.size(), this);
  }

  template <typename F, typename... Args>
  auto execute_forward(uint8_t tid, F &&f, Args &&...args)
      -> std::future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;
    tid %= count();
    auto &worker = workers[tid];
    auto task = new PTask<return_type()>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    std::future<return_type> res = task->get_future();
    {
      std::unique_lock<std::mutex> lock(worker.w_->mlck);

      // don't allow enqueueing after stopping the pool
      if (worker.stop)
        throw std::runtime_error("enqueue on stopped ThreadPool");

      worker.tasks.emplace([task]() {
        (*task)();
        delete task;
      });
    }
    worker.w_->cv.notify_one();
    return res;
  }

  template <typename F, typename... Args>
  auto execute(F &&f, Args &&...args)
      -> std::future<typename std::result_of<F(Args...)>::type> {
    uint64_t busy_bits_ = busy_bits.load(std::memory_order_relaxed);
    uint8_t _tid_ = ffsll(~busy_bits_);
    if (_tid_ == 0 || _tid_ > count()) {
      // no unused thread
      _tid_ = rng() % count();
    } else {
      --_tid_;
      // _tid_ is unused
    }
    return execute_forward(_tid_, std::forward<F>(f),
                           std::forward<Args>(args)...);
  }

  static const uint8_t max_thread_count = 64;

private:
  std::mt19937 rng;
  std::atomic<uint64_t> busy_bits;
  std::vector<Worker> workers;
};

#endif // __THREAD_POOL_H__
