#ifndef CACHE
#define CACHE

#include <iostream>
#include <list>
#include <mutex>
#include <unordered_map>
#include <vector>

struct CachedResponse {
  bool no_cache{false};
  bool must_revalidate{false};
  std::string e_tag{""};
  int status_code{0};
  std::string status_message{""};
  std::string server{""};
  std::string content_type{""};
  std::string body{""};
  std::chrono::steady_clock::time_point expiration_time;

  std::chrono::steady_clock::time_point get_expiration_time() { return expiration_time; }
  bool operator==(const CachedResponse & other) const;
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
  void put(const K & key, const V & value) {
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

  V get(const K & key) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto it = cache.find(key);
    if (it != cache.end()) {
      // Key found, move to front of LRU list and return value
      lru.erase(it->second.second);
      lru.push_front(key);
      it->second.second = lru.begin();
      return it->second.first;
    }
    // Key not found, return default value
    return V();
  }

  void remove(K key) {
    // Assume we want to remove the element with key "key_to_remove"
    auto it = cache.find(key);
    if (it != cache.end()) {
      // Erase the corresponding list iterator from the LRU list
      lru.erase(it->second.second);
      // Erase the element from the cache map
      cache.erase(it);
    }
  }
};

#endif  //CACHE