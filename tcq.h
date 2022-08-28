#ifndef __TCQ_H__
#define __TCQ_H__

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include "extend_mutex.h"

// Multi-Thread Task Combine Packing Queue
template <typename Tp, typename Rp, typename CTX> class TCQueue {
public:
  using T = Tp;
  using R = Rp;
  using CTX_t = CTX;
  using UID_t = uint64_t;

private:
  struct leader_handle;

public:
  // Task processing return handle
  class FutureHandle {
  public:
    /**
     * Get the task return value.
     * The task collection calls the `hook_batch_ret_collection` function once
     * to get the UIDs and task return values and their mapping relationships,
     * and then assigns these return values to the corresponding future_handle.
     * The handle is automatically recycled after this function is called.
     *
     * @param use_cv Whether to use `std::condition_variable` for waiting
     */
    virtual R task_get(bool use_cv = true) {
      if (use_cv) {
        std::unique_lock<std::mutex> lck(wait_lck);
        while (!complete_flag)
          wait_cv.wait(lck);
      } else {
        while (!complete_flag)
          std::this_thread::yield();
      }
      R ret = *reinterpret_cast<R *>(re_buf);
      qu->dealloc_future_handle(this);
      return ret;
    }

    FutureHandle(const FutureHandle &) = delete;
    FutureHandle &operator=(const FutureHandle &) = delete;

  protected:
    TCQueue *qu;
    char re_buf[sizeof(R)];
    bool use_cv;
    volatile bool complete_flag;
    std::mutex wait_lck;
    std::condition_variable wait_cv;

    friend class TCQueue;
    friend struct leader_handle;
    FutureHandle() : complete_flag(false), use_cv(true) {}
  };

private:
  struct leader_handle : public FutureHandle {
    R task_get(bool) {
      std::vector<std::pair<UID_t, R>> b = this->qu->hook_batch_ret_collection(
          *reinterpret_cast<CTX_t *>(uctx_buf), uid_map.size());
      if (b.size() != uid_map.size())
        throw "size error";

      // Assign the task return value to the corresponding future_handle.
      for (auto &p : b) {
        auto it = uid_map.find(p.first);
        if (it == uid_map.end())
          throw "uid error";
        uint32_t idx = it->second;
        FutureHandle *fh = fu_queue[idx];
        new (fh->re_buf) R(p.second);
        fh->complete_flag = true;
        if (fh->use_cv)
          fh->wait_cv.notify_one();
      }
      R ret = *reinterpret_cast<R *>(this->re_buf);
      this->qu->dealloc_leader_handle(this);
      return ret;
    }

    leader_handle(uint32_t max_count) : uid_map(max_count * 3 / 2) {
      uid_queue = new UID_t[max_count];
      task_queue = static_cast<T *>(::operator new(sizeof(T) * max_count));
      fu_queue = new FutureHandle *[max_count];
    }
    ~leader_handle() {
      delete[] uid_queue;
      delete[] task_queue;
      delete[] fu_queue;
      reinterpret_cast<CTX_t *>(uctx_buf)->~CTX_t();
    }

    std::unordered_map<UID_t, uint32_t> uid_map;
    UID_t *uid_queue;
    T *task_queue;
    FutureHandle **fu_queue;
    char uctx_buf[sizeof(CTX_t)];
  };

  // Get the serial number of the queue lock-freed.
  uint32_t enqueue_ready() {
    uint32_t cnt = queue_cnt_.load(std::memory_order_relaxed);
    while (1) {
      if (cnt == max_count)
        return -1;
      if (queue_cnt_.compare_exchange_weak(cnt, cnt + 1,
                                           std::memory_order_relaxed))
        return cnt;
      std::this_thread::yield();
    }
  }

  // Confirmation after queue insertion.
  void enqueue_confirm() { ++queue_cnt; }

  uint64_t gettime() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
  }

  FutureHandle *alloc_future_handle() {
    std::unique_lock<spin_mutex_u8> lck(fh_pool_lck);
    if (free_fh.empty())
      return new FutureHandle();
    FutureHandle *fh = free_fh.back();
    free_fh.pop_back();
    lck.unlock();
    fh->complete_flag = false;
    fh->use_cv = true;
    return fh;
  }

  void dealloc_future_handle(FutureHandle *fh) {
    std::unique_lock<spin_mutex_u8> lck(fh_pool_lck);
    free_fh.push_back(fh);
  }

  leader_handle *alloc_leader_handle() {
    std::unique_lock<spin_mutex_u8> lck(lh_pool_lck);
    if (free_lh.empty())
      return new leader_handle(max_count);
    leader_handle *lh = free_lh.back();
    free_lh.pop_back();
    lck.unlock();
    lh->uid_map.clear();
    lh->complete_flag = false;
    lh->use_cv = true;
    return lh;
  }

  void dealloc_leader_handle(leader_handle *lh) {
    std::unique_lock<spin_mutex_u8> lck(lh_pool_lck);
    free_lh.push_back(lh);
  }

public:
  /**
   * If the queue length reaches `max_count` or timeout `wait_us`, the
   * `hook_batch_collection` function is called after merging the current tasks
   * for processing. It is then retimed.
   *
   * @param max_count Maximum Queue Length
   * @param wait_us   Queue Wait Time
   */
  TCQueue(uint32_t max_count, uint32_t wait_us)
      : max_count(max_count), wait_us(wait_us), queue_valid(false) {
    free_fh.reserve(max_count);
  }
  ~TCQueue() {
    for (auto &fh : free_fh) {
      delete fh;
    }
    for (auto &lh : free_lh) {
      delete lh;
    }
  }
  TCQueue(const TCQueue &) = delete;
  TCQueue &operator=(const TCQueue &) = delete;

  /**
   * Adding tasks to the queue.
   * @param uid user-defined task id
   * @param task user task
   */
  FutureHandle &task_enqueue(UID_t uid, const T &task) {
    FutureHandle *_handle = nullptr;
  retry:
    // The purpose of `en_lck` is to distinguish between leader and follower.
    if (en_lck.try_lock()) {
      leader_handle *lh = alloc_leader_handle();
      queue_cnt_ = 0;
      queue_cnt = 0;
      fu_queue = lh->fu_queue;
      uid_queue = lh->uid_queue;
      task_queue = lh->task_queue;
      ++queue_ver;

      uint32_t idx = enqueue_ready();

      // Making the queue available.
      queue_valid = true;

      uint64_t time_s = gettime(), time_e;

      lh->qu = this;
      _handle = lh;
      uid_queue[idx] = uid;
      task_queue[idx] = task;
      fu_queue[idx] = lh;
      enqueue_confirm();

      // Polling to determine if the queue is full or timeout.
      do {
        std::this_thread::yield();
        time_e = gettime();
      } while (time_e - time_s < wait_us && queue_cnt < max_count);
      queue_valid = false;

      // Ensure that the current followers have finished queue operations or are
      // waiting.
      wlck.lock();
      wlck.unlock();

      uint32_t len = queue_cnt;
      en_lck.unlock();

      new (lh->uctx_buf)
          CTX_t(hook_batch_collection(lh->uid_queue, task_queue, len));

      // Mapping UIDs to tasks.
      for (uint32_t i = 0; i < len; ++i) {
        lh->uid_map.insert(std::make_pair(lh->uid_queue[i], i));
      }
    } else {
      // The purpose of double-locking is to prevent access to the queue from
      // continuing even after the queue has failed.
      if (!queue_valid) {
        std::this_thread::yield();
        goto retry;
      }
      wlck.lock_shared();
      if (!queue_valid) {
        wlck.unlock_shared();
        std::this_thread::yield();
        goto retry;
      }

      uint32_t idx = enqueue_ready();
      if (idx == -1) {
        uint8_t _ver = static_cast<uint8_t>(queue_ver);
        wlck.unlock_shared();
        // If the queue is full, wait for the queue to expire before retrying.
        // There is a possibility that the queue will fail immediately after
        // being created by a new leader (ABA problem), so add `queue_ver` to
        // prevent this problem.
        while (queue_valid && _ver == queue_ver)
          std::this_thread::yield();
        goto retry;
      }

      FutureHandle *fh = alloc_future_handle();
      fh->qu = this;
      _handle = fh;

      uid_queue[idx] = uid;
      task_queue[idx] = task;
      fu_queue[idx] = fh;
      enqueue_confirm();

      wlck.unlock_shared();
    }
    return *_handle;
  }

  // Processes all tasks in the queue. For example, packing and send().
  std::function<CTX_t(const UID_t *uids, const T *tasks, uint32_t len)>
      hook_batch_collection;
  // Get the mapping of UID to the return value of a task. For example, recv()
  // and parsing.
  std::function<std::vector<std::pair<UID_t, R>>(CTX_t &ctx, uint32_t need_len)>
      hook_batch_ret_collection;

private:
  spin_mutex_u8 en_lck;
  shared_mutex_u8 wlck;
  volatile bool queue_valid;
  volatile uint8_t queue_ver;
  uint32_t max_count;
  uint32_t wait_us;
  UID_t *uid_queue;
  T *task_queue;
  FutureHandle **fu_queue;
  std::atomic<uint32_t> queue_cnt_;
  std::atomic<uint32_t> queue_cnt;

  // Handle recycling bin
  spin_mutex_u8 fh_pool_lck;
  std::vector<FutureHandle *> free_fh;
  char padding[64 - sizeof(fh_pool_lck) - sizeof(free_fh)];
  spin_mutex_u8 lh_pool_lck;
  std::vector<leader_handle *> free_lh;
};

#endif // __TCQ_H__
