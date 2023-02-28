#ifndef HTTP_HANDLER
#define HTTP_HANDLER
#include <boost/algorithm/string.hpp>
#include <boost/beast.hpp>

#include "cache.hpp"
namespace beast = boost::beast;  // from <boost/beast.hpp>
namespace http = beast::http;    // from <boost/beast/http.hpp>

class HttpParser {
 public:
  http::response<http::string_body> parse_cachedResponse(CachedResponse & cached_resp) {
    http::response<http::string_body> res{beast::http::status::ok, 11};
    res.result(static_cast<beast::http::status>(cached_resp.status_code));
    res.reason(cached_resp.status_message);
    res.set(beast::http::field::server, cached_resp.server);
    res.set(beast::http::field::content_type, cached_resp.content_type);
    res.body() = cached_resp.body;
    res.prepare_payload();
    return res;
  }

  std::pair<std::string, std::string> get_server_name(
    http::request<http::string_body> & request) {
    std::string host;
    std::string port = "80";
    const auto & headers = request.base();
    const auto & host_field = headers[boost::beast::http::field::host];
    // Split the value of the Cache-Control header into individual directives
    std::size_t colon_pos = host_field.find(":");
    if (colon_pos != std::string::npos) {
      port = std::string(host_field.substr(colon_pos + 1));
      host = std::string(host_field.substr(0, colon_pos));
    }
    else {
      host = std::string(host_field);
    }
    return std::make_pair(host, port);
  }

  std::string get_cache_key(const http::request<http::string_body> & req) {
    std::string cache_key = req.method_string().data();
    cache_key.append(req.target().data(), req.target().size());
    return cache_key;
  }

  CachedResponse parse_response(const http::response<http::string_body> & resp) {
    CachedResponse cached_resp;
    //store status_code
    cached_resp.status_code = resp.result_int();
    //store the message
    cached_resp.status_message = std::string(resp.reason());
    //store body
    cached_resp.body = resp.body();
    //store e-tag
    const auto & headers = resp.base();
    auto it = headers.find(http::field::etag);
    if (it != headers.end()) {
      cached_resp.e_tag = std::string(it->value());
      // use the e_tag value as needed
    }
    //store content type
    auto content_type = resp.find(http::field::content_type);
    if (content_type != resp.end()) {
      cached_resp.content_type = std::string(content_type->value());
    }
    else {
      cached_resp.content_type = "";
    }
    //store server
    auto server = resp.find(http::field::server);
    if (content_type != resp.end()) {
      cached_resp.server = std::string(server->value());
    }
    else {
      cached_resp.server = "";
    }
    //store fresh time and expiration time
    // Check if the response has a Cache-Control header
    if (headers.find(boost::beast::http::field::cache_control) != headers.end()) {
      // Get the value of the Cache-Control header
      const auto & cache_control = headers[boost::beast::http::field::cache_control];
      // Split the value of the Cache-Control header into individual directives
      std::vector<std::string> directives;
      boost::split(directives, std::string(cache_control), boost::is_any_of(","));
      // Find the max-age directive in the Cache-Control header
      auto it = std::find_if(
          directives.begin(), directives.end(), [](const std::string & directive) {
            return boost::starts_with(directive, "max-age=");
          });
      if (it != directives.end()) {
        // Get the value of the max-age directive
        std::string max_age_str = it->substr(std::string("max-age=").size());
        int max_age_sec = std::stoi(max_age_str);

        // Calculate the maximum age in seconds
        std::chrono::seconds max_age(max_age_sec);

        // Calculate the expiration time of the response
        cached_resp.expiration_time = std::chrono::steady_clock::now() + max_age;
      }
      //fint the must-revalidate directive in the Cache-Control header
      it = std::find_if(
          directives.begin(), directives.end(), [](const std::string & directive) {
            return directive == "must-revalidate";
          });

      if (it != directives.end()) {
        // The response has the must-revalidate directive
        cached_resp.must_revalidate = true;
      }
      else {
        cached_resp.must_revalidate = false;
      }
      //fint the no_cache directive in the Cache-Control header
      it = std::find_if(
          directives.begin(), directives.end(), [](const std::string & directive) {
            return directive == "no-cache";
          });

      if (it != directives.end()) {
        // The response has the must-revalidate directive
        cached_resp.no_cache = true;
      }
      else {
        cached_resp.no_cache = false;
      }
      //fint the max-stale directive in the Cache-Control header
      it = std::find_if(
          directives.begin(), directives.end(), [](const std::string & directive) {
            return boost::starts_with(directive, "max-stale=");
          });
      if (it != directives.end()) {
        // Get the value of the max-stale directive
        std::string max_stale_str = it->substr(std::string("max-stale=").size());
        int max_stale_sec = std::stoi(max_stale_str);

        // Calculate the maximum stale in seconds
        std::chrono::seconds max_stale(max_stale_sec);

        // Calculate the expiration time of the response
        cached_resp.expiration_time = std::chrono::steady_clock::now() + max_stale;
      }
    }
    return cached_resp;
  }
};
#endif  //HTTP_HANDLER