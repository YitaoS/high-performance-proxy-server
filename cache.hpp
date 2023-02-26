#include <iostream>
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

class CachedResponse {
 public:
  bool must_revalidate{false};
  int status_code{0};
  std::string status_message{""};
  std::string server{""};
  std::string content_type{""};
  std::string body{""};
  std::chrono::steady_clock::time_point fresh_time;
  std::chrono::steady_clock::time_point expiration_time;

 public:
  bool operator==(const CachedResponse & other) const {
    return must_revalidate == other.must_revalidate && status_code == other.status_code &&
           status_message == other.status_message && server == other.server &&
           content_type == other.content_type && body == other.body &&
           fresh_time == other.fresh_time && expiration_time == other.expiration_time;
  }
  bool operator!=(const CachedResponse & other) const { return !(*this == other); }
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

  void remove(K key) {
    // Assume we want to remove the element with key "key_to_remove"
    auto it = cache.find(key);
    if (it != cache.end()) {
      // Erase the corresponding list iterator from the LRU list
      lru_list.erase(it->second.second);

      // Erase the element from the cache map
      cache.erase(it);
    }
  }
};
