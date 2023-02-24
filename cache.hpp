#include <iostream>
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

class CachedResponse {
  std::vector<uint8_t> body;
  std::chrono::steady_clock::time_point expiration_time;
};

/**
 * this is a cahce implement LRU principle
 * response cache: let K be std::string, let V be CachedResponse
*/
template<typename K, typename V>
class Cache {
 private:
  size_t capacity;
  std::mutex cache_mutex;
  std::unordered_map<K, std::pair<V, typename std::list<K>::iterator> > cache;
  std::list<K> lru;

 public:
  Cache(size_t capacity) : capacity(capacity) {}

  void put(K key, V value) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto it = cache.find(key);
    if (it != cache.end()) {
      // Key already exists, update value and move to front of LRU list
      lru.erase(it->second.second);
      lru.push_front(key);
      it->second = {value, lru.begin()};
    }
    else {
      // Key does not exist, add to cache and front of LRU list
      if (cache.size() == capacity) {
        // Cache is full, remove least recently used item
        cache.erase(lru.back());
        lru.pop_back();
      }
      lru.push_front(key);
      cache[key] = {value, lru.begin()};
    }
  }

  V get(K key) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto it = cache.find(key);
    if (it != cache.end()) {
      // Key found, move to front of LRU list and return value
      lru.erase(it->second.second);
      lru.push_front(key);
      it->second.second = lru.begin();
      V res = it->second.first;
      return res;
    }
    // Key not found, return default value
    return V();
  }
};
