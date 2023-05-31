#include "function_traits.h"
#include <functional>
#include <list>
#include <unordered_map>

template <typename K, typename V, typename _Hash = std::hash<K>,
          typename _Pred = std::equal_to<K>,
          typename _Alloc = std::allocator<std::pair<const K, V>>>
class LRUCache {
public:
  LRUCache(size_t capacity) : capacity(capacity) {}

  void put(const K &key, const V &value) {
    auto it = cache.find(key);
    if (it != cache.end()) {
      // key already exists, update value and move to front
      it->second.second = value;
      lru_list.splice(lru_list.begin(), lru_list, it->second.first);
    } else {
      // key does not exist, insert new entry
      if (cache.size() == capacity) {
        // cache is full, evict least recently used entry
        cache.erase(lru_list.back());
        lru_list.splice(lru_list.begin(), lru_list, --lru_list.end());
        lru_list.front() = key;
      } else {
        lru_list.push_front(key);
      }
      cache[key] = {lru_list.begin(), value};
    }
  }

  bool get(const K &key, V &value) {
    auto it = cache.find(key);
    if (it == cache.end()) {
      // key does not exist
      return false;
    }

    // key exists, move to front and return value
    lru_list.splice(lru_list.begin(), lru_list, it->second.first);
    value = it->second.second;
    return true;
  }

private:
  size_t capacity;
  std::unordered_map<K, std::pair<typename std::list<K>::iterator, V>, _Hash,
                     _Pred, _Alloc>
      cache;
  std::list<K, _Alloc> lru_list;
};

template <std::size_t N> struct tuple_hash_helper {
  template <typename... P>
  inline std::size_t operator()(const std::tuple<P...> &tu) const {
    using T = typename std::tuple_element<N, std::tuple<P...>>::type;
    std::size_t r = tuple_hash_helper<N - 1>()(tu);
    return r ^ (std::hash<T>()(std::get<N>(tu)) + (r << 6) + (r >> 2) +
                0x9e3779b97f4a7c15);
  }
};

template <> struct tuple_hash_helper<0> {
  template <typename... P>
  inline std::size_t operator()(const std::tuple<P...> &tu) const {
    using T = typename std::tuple_element<0, std::tuple<P...>>::type;
    return std::hash<T>()(std::get<0>(tu));
  }
};

template <typename F> class cached_bind_helper {
  using result_type = typename function_traits<F>::result_type;
  using args_tuple_type = typename function_traits<F>::args_tuple_type;

public:
  cached_bind_helper(F &&fn) : fn(std::move(fn)) {}

  template <typename... Args> result_type operator()(Args &&...args) {
    args_tuple_type tu(args...);
    auto it = cache_map.find(tu);
    if (it != cache_map.end())
      return it->second;
    return cache_map[tu] = fn(std::move(args)...);
  }

private:
  struct Hash {
    std::size_t operator()(const args_tuple_type &tu) const {
      return tuple_hash_helper<std::tuple_size<args_tuple_type>::value - 1>()(
          tu);
    }
  };

  F fn;
  std::unordered_map<args_tuple_type, result_type, Hash> cache_map;
};

template <typename F, typename... Args>
auto cached_bind(F &&fn, Args &&...args) {
  return std::bind(cached_bind_helper<F>(std::move(fn)), std::move(args)...);
}