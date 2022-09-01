#ifndef __DTXN_BASE_H__
#define __DTXN_BASE_H__

#include <unordered_map>
#include <unordered_set>
#include <vector>

template <typename Key_t, typename Value_t> struct dtxn_base {
  using txn_id_t = uint64_t;
  using timestamp_t = uint64_t;

  static constexpr timestamp_t max_timestamp = UINT64_MAX;

  enum status_t {
    ok,
    not_found,
    locked,
    log_error,
    lock_error,
    validate_error,
    apply_error,
    aborted,
  };

  struct txn_cache_item_t {
    bool dirty;
    bool wo;
    timestamp_t current_read_ts;
    Value_t val;
  };

  struct mvocc_txn {
    status_t begin() {
      if (!base)
        return aborted;

      txn_op_cache.clear();
      read_ts = base->get_ts();
      return ok;
    }
    status_t get(const Key_t &key, Value_t &val) {
      if (!base)
        return aborted;

      auto it = txn_op_cache.find(key);
      if (it == txn_op_cache.end()) {
        timestamp_t current_read_ts;
        status_t s = base->get_val(key, read_ts, val, current_read_ts);
        if (s != ok) {
          return s;
        }
        txn_op_cache.emplace(
            key, txn_cache_item_t{false, false, current_read_ts, val});
      } else {
        val = it->second.val;
      }
      return ok;
    }
    status_t put(const Key_t &key, const Value_t &val) {
      if (!base)
        return aborted;

      auto it = txn_op_cache.find(key);
      if (it == txn_op_cache.end()) {
        txn_op_cache.emplace(key,
                             txn_cache_item_t{true, true, max_timestamp, val});
      } else {
        it->second.dirty = true;
        it->second.val = val;
      }
      return ok;
    }
    status_t commit() {
      if (!base)
        return aborted;

      status_t s;
      bool is_ro_tx = false;
      std::vector<Key_t> inserted_keys;
      std::vector<std::pair<Key_t, Value_t>> write_set;
      std::unordered_set<Key_t> locked_keys(txn_op_cache.size() * 3 / 2);

      // 1. lock
      for (auto &p : txn_op_cache) {
        if (p.second.dirty) {
          s = base->lock_key(p.first);
          if (s == not_found) {
            s = base->insert_and_lock_empty_key_slot(p.first);
            if (s == ok) {
              inserted_keys.push_back(p.first);
            }
          }
          if (s != ok) {
            s = lock_error;
            goto release_lock;
          }
          locked_keys.insert(p.first);
        }
      }

      is_ro_tx = locked_keys.empty();

      if (!is_ro_tx) {
        // 2. validate
        for (auto &p : txn_op_cache) {
          if (!p.second.wo) {
            timestamp_t current_ts;
            s = base->get_key_lastest_ts(p.first, current_ts);
            if (!((p.second.dirty && s == locked) || s == ok)) {
              s = validate_error;
              goto release_lock;
            }
            if (current_ts != p.second.current_read_ts) {
              s = validate_error;
              goto release_lock;
            }
          }
        }

        // 3. redo log
        write_ts = base->get_ts();
        for (auto &key : locked_keys) {
          write_set.emplace_back(key, txn_op_cache.find(key)->second.val);
        }
        s = base->write_redo_log(txn_id, write_ts, write_set);
        if (s != ok) {
          s = log_error;
          goto release_lock;
        }

        // 4. commit
        for (auto &p : write_set) {
          s = base->put_val(p.first, write_ts, p.second);
          if (s != ok) {
            s = apply_error;
            goto release_lock;
          }
          base->unlock_key(p.first);
          locked_keys.erase(p.first);
        }
      }

      s = ok;
      goto finish;

    release_lock:
      for (auto &key : inserted_keys) {
        base->erase_empty_key_slot(key);
        locked_keys.erase(key);
      }
      for (auto &key : locked_keys) {
        base->unlock_key(key);
      }

    finish:
      base = nullptr;
      return s;
    }
    status_t abort() {
      base = nullptr;
      return aborted;
    }
    template <typename F, typename... Args>
    status_t auto_run(F &&f, Args &&...args) {
      status_t s;
      do {
        base = rep_base;
        s = f(std::forward<Args>(args)...);
      } while (s != ok);
      return ok;
    }

    txn_id_t txn_id;
    dtxn_base *base;
    dtxn_base *rep_base;
    timestamp_t read_ts;
    timestamp_t write_ts;
    std::unordered_map<Key_t, txn_cache_item_t> txn_op_cache;
  };

  mvocc_txn generate_mvocc_txn() {
    mvocc_txn tx;
    tx.base = this;
    tx.rep_base = this;
    tx.txn_id = get_txn_id();
    return tx;
  }

  virtual txn_id_t get_txn_id() = 0;
  virtual timestamp_t get_ts() = 0;
  virtual status_t insert_and_lock_empty_key_slot(const Key_t &key) = 0;
  virtual status_t erase_empty_key_slot(const Key_t &key) = 0;
  virtual status_t get_val(const Key_t &key, timestamp_t read_ts, Value_t &val,
                           timestamp_t &current_ts) = 0;
  virtual status_t put_val(const Key_t &key, timestamp_t write_ts,
                           const Value_t &val) = 0;
  virtual status_t lock_key(const Key_t &key) = 0;
  virtual status_t unlock_key(const Key_t &key) = 0;
  virtual status_t get_key_lastest_ts(const Key_t &key,
                                      timestamp_t &current_ts) = 0;
  virtual status_t
  write_redo_log(txn_id_t txn_id, timestamp_t write_ts,
                 const std::vector<std::pair<Key_t, Value_t>> &write_set) = 0;
};

#include "extend_mutex.h"
#include <atomic>
#include <list>

struct SimpleTxnManager : public dtxn_base<int, int> {
  using Key_t = int;
  using Value_t = int;

  struct val_ver_node {
    uint64_t ts;
    int val;
  };

  struct val_ver_node_head {
    volatile bool lock = false;
    std::list<val_ver_node> ver_list;
  };

  static constexpr int lock_hash_max = 512;
  shared_mutex map_lock[lock_hash_max];
  std::unordered_map<int, val_ver_node_head> hmap[lock_hash_max];
  std::atomic<uint64_t> ts_gen = {0};
  std::atomic<uint64_t> tid_gen = {0};

  virtual txn_id_t get_txn_id() override { return ++tid_gen; }
  virtual timestamp_t get_ts() override { return ++ts_gen; }
  virtual status_t get_val(const Key_t &key, timestamp_t read_ts, Value_t &val,
                           timestamp_t &current_ts) override {
    auto &map_lock = this->map_lock[std::hash<Key_t>()(key) % lock_hash_max];
    auto &hmap = this->hmap[std::hash<Key_t>()(key) % lock_hash_max];
    map_lock.lock_shared();
    auto it = hmap.find(key);
    if (it == hmap.end()) {
      map_lock.unlock();
      return not_found;
    }
    auto &node = it->second;
    map_lock.unlock();

    if (node.lock && node.ver_list.begin()->ts <= read_ts) {
      return locked;
    }
    status_t s;
    for (auto &n : node.ver_list) {
      if (n.ts <= read_ts) {
        val = n.val;
        current_ts = n.ts;
        return ok;
      }
    }

    return not_found;
  }
  virtual status_t insert_and_lock_empty_key_slot(const Key_t &key) override {
    status_t s;
    auto &map_lock = this->map_lock[std::hash<Key_t>()(key) % lock_hash_max];
    auto &hmap = this->hmap[std::hash<Key_t>()(key) % lock_hash_max];
    map_lock.lock();
    auto it = hmap.find(key);
    if (it == hmap.end()) {
      it = hmap.emplace(key, val_ver_node_head()).first;
      auto &node = it->second;
      __atomic_exchange_n(&node.lock, true, __ATOMIC_ACQUIRE);
      s = ok;
    } else {
      s = locked;
    }
    map_lock.unlock();
    return s;
  }
  virtual status_t erase_empty_key_slot(const Key_t &key) override {
    auto &map_lock = this->map_lock[std::hash<Key_t>()(key) % lock_hash_max];
    auto &hmap = this->hmap[std::hash<Key_t>()(key) % lock_hash_max];
    map_lock.lock();
    auto it = hmap.find(key);
    if (it == hmap.end()) {
      map_lock.unlock();
      return not_found;
    }
    hmap.erase(it);
    map_lock.unlock();
    return ok;
  }
  virtual status_t put_val(const Key_t &key, timestamp_t write_ts,
                           const Value_t &val) override {
    auto &map_lock = this->map_lock[std::hash<Key_t>()(key) % lock_hash_max];
    auto &hmap = this->hmap[std::hash<Key_t>()(key) % lock_hash_max];
    map_lock.lock();
    auto it = hmap.find(key);
    if (it == hmap.end()) {
      it = hmap.emplace(key, val_ver_node_head()).first;
    }
    auto &node = it->second;
    map_lock.unlock();

    for (auto it = node.ver_list.begin(); it != node.ver_list.end(); ++it) {
      auto &n = *it;
      if (n.ts <= write_ts) {
        node.ver_list.insert(it, val_ver_node{write_ts, val});
        break;
      }
    }

    if (node.ver_list.empty()) {
      node.ver_list.insert(node.ver_list.begin(), val_ver_node{write_ts, val});
    }
    return ok;
  }
  virtual status_t lock_key(const Key_t &key) override {
    auto &map_lock = this->map_lock[std::hash<Key_t>()(key) % lock_hash_max];
    auto &hmap = this->hmap[std::hash<Key_t>()(key) % lock_hash_max];
    map_lock.lock_shared();
    auto it = hmap.find(key);
    if (it == hmap.end()) {
      map_lock.unlock();
      return not_found;
    }
    auto &node = it->second;
    map_lock.unlock();

    bool old = __atomic_exchange_n(&node.lock, true, __ATOMIC_ACQUIRE);
    if (old) {
      return locked;
    }
    return ok;
  }
  virtual status_t unlock_key(const Key_t &key) override {
    auto &map_lock = this->map_lock[std::hash<Key_t>()(key) % lock_hash_max];
    auto &hmap = this->hmap[std::hash<Key_t>()(key) % lock_hash_max];
    map_lock.lock_shared();
    auto it = hmap.find(key);
    if (it == hmap.end()) {
      map_lock.unlock();
      return not_found;
    }
    auto &node = it->second;
    map_lock.unlock();

    __atomic_store_n(&node.lock, false, __ATOMIC_RELEASE);
    return ok;
  }
  virtual status_t get_key_lastest_ts(const Key_t &key,
                                      timestamp_t &current_ts) override {
    auto &map_lock = this->map_lock[std::hash<Key_t>()(key) % lock_hash_max];
    auto &hmap = this->hmap[std::hash<Key_t>()(key) % lock_hash_max];
    status_t s;
    map_lock.lock_shared();
    auto it = hmap.find(key);
    if (it == hmap.end()) {
      map_lock.unlock();
      return not_found;
    }
    auto &node = it->second;
    map_lock.unlock();

    s = ok;
    if (node.lock) {
      s = locked;
    }

    current_ts = node.ver_list.begin()->ts;
    return s;
  }
  virtual status_t write_redo_log(
      txn_id_t txn_id, timestamp_t write_ts,
      const std::vector<std::pair<Key_t, Value_t>> &write_set) override {
    return ok;
  }
};

#endif // __DTXN_BASE_H__
