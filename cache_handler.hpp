#ifndef CACHE_HANDLER
#define CACHE_HANDLER

#include <boost/algorithm/string.hpp>
#include <boost/beast.hpp>

#include "cache.hpp"
#include "log_writer.hpp"

namespace beast = boost::beast;  // from <boost/beast.hpp>
namespace http = beast::http;    // from <boost/beast/http.hpp>
class CacheHandler {
  Cache<std::string, CachedResponse> & http_cache;
  LogWriter & lw_;

 public:
  explicit CacheHandler(Cache<std::string, CachedResponse> & cache, LogWriter & lw) :
      http_cache(cache), lw_(lw) {}

  void cache_response(std::string cache_key, CachedResponse cache_value);

  CachedResponse get(std::string key);

  void remove(std::string key);

  bool can_be_cached(const http::response<http::string_body> & resp);

  std::string cached_response_state(const CachedResponse & cr,
                                    const http::request<http::string_body> & req);

  CachedResponse get_cached_response(std::string cache_key);
};

#endif  // CACHE_HANDLER