#include "cache_handler.hpp"

void CacheHandler::cache_response(std::string cache_key, CachedResponse cache_value) {
    http_cache.put(cache_key, cache_value);
}

CachedResponse CacheHandler::get(std::string key) { 
    return http_cache.get(key); 
}

