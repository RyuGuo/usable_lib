#ifndef __TCQ_H__
#define __TCQ_H__

#include "extend_mutex.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

// Multi-Thread Task Combine Packing Queue
template <typename Task, typename Result, typename Alloc = std::allocator<void>>
class TCQueue {
public:
  using UID_t = uint64_t;

private:
  struct future_state;
  struct future_leader_state;

public:
  class future {
  public:
    /**
     * Get the task return value.
     * The task collection calls the `hook_batch_ret_collection` function once
     * to get the UIDs and task return values and their mapping relationships,
     * and then assigns these return values to the corresponding future_handle.
     * The handle is automatically recycled after this function is called.
     */
    Result get() { return state->get_(); }

    future() = default;
    future(future &&other) : future() { swap(std::move(other)); }
    future &operator=(future &&other) { swap(std::move(other)); }

    future(const future &) = delete;
    future &operator=(const future &) = delete;

    void swap(future &&other) { state.swap(other.state); }

  private:
    std::unique_ptr<future_state> state;

    friend class TCQueue;
  };

private:
  template <typename U>
  using rebind_alloc =
      typename std::allocator_traits<Alloc>::template rebind_alloc<U>;

  using result_alloc_type = rebind_alloc<Result>;
  using future_state_alloc_type = rebind_alloc<future_state>;
  using future_leader_state_alloc_type = rebind_alloc<future_leader_state>;
  using uid_alloc_type = rebind_alloc<UID_t>;
  using task_alloc_type = rebind_alloc<Task>;
  using promise_result_alloc_type = rebind_alloc<std::promise<Result>>;

  struct future_state {
    virtual Result get_() { return fu.get(); }

    virtual ~future_state() {}

    void *operator new(size_t n) {
      return future_state_alloc_type().allocate(n);
    }

    void operator delete(void *ptr, size_t n) {
      return future_state_alloc_type().deallocate(
          static_cast<future_state *>(ptr), n);
    }

    std::future<Result> fu;
  };

  struct future_leader_state : public future_state {
    virtual Result get_() override {
      process_batch();
      return future_state::get_();
    }

    future_leader_state(TCQueue &qu)
        : processed(false), qu(qu), uid_map(qu.max_count * 3 / 2),
          pro_queue(qu.max_count) {}

    void process_batch() {
      if (processed)
        return;

      std::vector<std::pair<UID_t, Result>> b =
          qu.hook_batch_ret_collection(uctx_ptr, uid_map.size());
      if (b.size() != uid_map.size())
        throw "size error";

      // Assign the task return value to the corresponding future.
      for (auto &p : b) {
        auto it = uid_map.find(p.first);
        if (it == uid_map.end())
          throw "uid error";
        uint32_t idx = it->second;

        std::promise<Result> &pro = pro_queue[idx];
        pro.set_value(p.second);
      }

      processed = true;
    }

    void *operator new(size_t n) {
      return future_leader_state_alloc_type().allocate(n);
    }

    void operator delete(void *ptr, size_t n) {
      return future_leader_state_alloc_type().deallocate(
          static_cast<future_leader_state *>(ptr), n);
    }

    std::unordered_map<UID_t, uint32_t, std::hash<UID_t>, std::equal_to<UID_t>,
                       rebind_alloc<std::pair<UID_t, uint32_t>>>
        uid_map;
    std::vector<std::promise<Result>, promise_result_alloc_type> pro_queue;

    TCQueue &qu;
    void *uctx_ptr;

    bool processed;
  };

  // Get the serial number of the queue lock-freed.
  uint32_t enqueue_ready() {
    uint32_t cnt = queue_cnt.fetch_add(1, std::memory_order_acquire);
    if (cnt >= max_count)
      return -1;
    return cnt;
  }

  // Confirmation after queue insertion.
  void enqueue_confirm() {
    queue_try_cnt.fetch_add(1, std::memory_order_release);
  }

  uint64_t get_time() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
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
      : queue_valid(false), max_count(max_count), wait_us(wait_us) {}
  ~TCQueue() {}

  TCQueue(const TCQueue &) = delete;
  TCQueue &operator=(const TCQueue &) = delete;

  /**
   * Adding tasks to the queue.
   * @param uid user-defined task id
   * @param task user task
   */
  future task_enqueue(UID_t uid, Task &&task) {
    std::promise<Result> pro(std::allocator_arg, result_alloc_type());
    future fu;

  retry:
    // The purpose of `en_lck` is to distinguish between leader and follower.
    if (en_lck.try_lock()) {
      auto fls = std::make_unique<future_leader_state>(*this);

      // Until `queue_valid` is set to true, other threads will keep retrying,
      // so queues can be initialised thread-safely.

      std::vector<UID_t, uid_alloc_type> uid_queue_(max_count);
      std::vector<Task, task_alloc_type> task_queue_(max_count);

      queue_cnt.store(0, std::memory_order_relaxed);
      queue_try_cnt.store(0, std::memory_order_relaxed);
      uid_queue.swap(uid_queue_);
      task_queue.swap(task_queue_);
      pro_queue.swap(fls->pro_queue);
      ++queue_ver;

      // The leader must be able to queue tasks, so it needs to get the `idx`
      // before `queue_valid` is set to true.
      uint32_t idx = enqueue_ready();

      // Making the queues available.
      queue_valid = true;

      uint64_t time_s = get_time(), time_e;

      fls->fu = pro.get_future();

      uid_queue[idx] = std::move(uid);
      task_queue[idx] = std::move(task);
      pro_queue[idx] = std::move(pro);

      enqueue_confirm();

      // Polling to determine if the queue is full or timeout.
      do {
        std::this_thread::yield();
        time_e = get_time();
      } while (time_e - time_s < wait_us &&
               queue_try_cnt.load(std::memory_order_relaxed) < max_count);

      queue_valid = false;

      // Ensure that the current followers have finished queue operations or are
      // waiting (etc. `if (!queue_valid) goto retry;` ).
      wlck.lock();
      wlck.unlock();

      // We can hold queues thread-safely until `en_lck` unlocking.

      uint32_t len = queue_try_cnt.load(std::memory_order_relaxed);

      uid_queue.swap(uid_queue_);
      task_queue.swap(task_queue_);
      pro_queue.swap(fls->pro_queue);

      en_lck.unlock();

      uid_queue_.resize(len);
      task_queue_.resize(len);
      fls->pro_queue.resize(len);

      fls->uctx_ptr = hook_batch_collection(uid_queue_, task_queue_);

      // Mapping UIDs to tasks.
      for (uint32_t i = 0; i < len; ++i) {
        fls->uid_map.insert(std::make_pair(uid_queue_[i], i));
      }

      fu.state = std::move(fls);
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
      if (idx == -1u) {
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

      fu.state = std::make_unique<future_state>();
      fu.state->fu = pro.get_future();

      uid_queue[idx] = std::move(uid);
      task_queue[idx] = std::move(task);
      pro_queue[idx] = std::move(pro);

      enqueue_confirm();

      wlck.unlock_shared();
    }

    return fu;
  }

  // Processes all tasks in the queue. For example, packing and send().
  std::function<void *(const std::vector<UID_t, uid_alloc_type> &uids,
                       const std::vector<Task, task_alloc_type> &tasks)>
      hook_batch_collection;

  // Get the mapping of UID to the return value of a task. For example, recv()
  // and parsing.
  std::function<std::vector<std::pair<UID_t, Result>,
                            rebind_alloc<std::pair<UID_t, Result>>>(
      void *ctx, uint32_t need_len)>
      hook_batch_ret_collection;

private:
  spin_mutex_b8 en_lck;
  shared_mutex_b8 wlck;
  volatile bool queue_valid;
  volatile uint8_t queue_ver;
  const uint32_t max_count;
  const uint32_t wait_us;

  std::vector<UID_t, uid_alloc_type> uid_queue;
  std::vector<Task, task_alloc_type> task_queue;
  std::vector<std::promise<Result>, promise_result_alloc_type> pro_queue;

  std::atomic<uint32_t> queue_cnt;
  std::atomic<uint32_t> queue_try_cnt;
};

#endif // __TCQ_H__
