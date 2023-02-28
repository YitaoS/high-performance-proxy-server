#ifndef HTTP_HANDLER
#define HTTP_HANDLER
#include <boost/algorithm/string.hpp>
#include <boost/beast.hpp>

#include "cache.hpp"
namespace beast = boost::beast;  // from <boost/beast.hpp>
namespace http = beast::http;    // from <boost/beast/http.hpp>

class HttpParser {
 public:
  http::response<http::string_body> parse_cachedResponse(CachedResponse & cached_resp);

  std::pair<std::string, std::string> get_server_name(http::request<http::string_body> & request);

  std::string get_cache_key(const http::request<http::string_body> & req);

  CachedResponse parse_response(const http::response<http::string_body> & resp);
};
#endif  //HTTP_HANDLER