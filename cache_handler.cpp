#include "cache_handler.hpp"

void CacheHandler::cache_response(std::string cache_key, CachedResponse cache_value) {
    http_cache.put(cache_key, cache_value);
}

CachedResponse CacheHandler::get(std::string key) { 
    return http_cache.get(key); 
}

void CacheHandler::remove(std::string key) {
    http_cache.remove(key); 
}

bool CacheHandler::can_be_cached(const http::response<http::string_body> & resp) {
    // log: ID: not cacheable because REASON
    // ID: cached, expires at EXPIRES
    // ID: cached, but requires re-validation
    const auto & headers = resp.base();
    if (resp.chunked() == true) {
      lw_.log_not_cacheable("response.chunked() = true");
      return false;
    }
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
            return directive == "no-store";
          });
      if (it != directives.end()) {
        // The response has the must-revalidate directive
        lw_.log_not_cacheable("no-store in the header");
        return false;
      }
      it = std::find_if(
          directives.begin(), directives.end(), [](const std::string & directive) {
            return directive == "private";
          });
      if (it != directives.end()) {
        // The response has the must-revalidate directive
        lw_.log_not_cacheable("private in the header");
        return false;
      }
      it = std::find_if(
          directives.begin(), directives.end(), [](const std::string & directive) {
            return directive == "no-cache";
          });
      if (it != directives.end()) {
        // The response has the must-revalidate directive
        lw_.log_cached_with_revalidation();
        return true;
      }
      std::chrono::steady_clock::time_point expiration_time;
      it = std::find_if(
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
        expiration_time = std::chrono::steady_clock::now() + max_age;
      }
      lw_.log_cached_with_expire_time(expiration_time);
      return true;
    }
    else {
      return false;
    }
  }

std::string CacheHandler::cached_response_state(const CachedResponse & cr,
                                    const http::request<http::string_body> & req) {
    if (cr.no_cache == true) {
      return "must-revalidate";
    }
    if (cr.must_revalidate == true &&
        std::chrono::steady_clock::now() > cr.expiration_time) {
      return "must-revalidate";
    }

    int min_fresh_sec = -1;
    int max_stale_sec = -1;
    //get the time
    const auto & headers = req.base();
    const auto & cache_control = headers[boost::beast::http::field::cache_control];
    // Split the value of the Cache-Control header into individual directives
    std::vector<std::string> directives;
    boost::split(directives, std::string(cache_control), boost::is_any_of(","));
    // Find the min-fresh directive in the Cache-Control header
    auto it = std::find_if(
        directives.begin(), directives.end(), [](const std::string & directive) {
          return boost::starts_with(directive, "min-fresh=");
        });
    if (it != directives.end()) {
      // Get the value of the max-age directive
      std::string min_fresh_str = it->substr(std::string("min-fresh=").size());
      min_fresh_sec = std::stoi(min_fresh_str);
    }
    // Find the max-stale directive in the Cache-Control header
    it = std::find_if(
        directives.begin(), directives.end(), [](const std::string & directive) {
          return boost::starts_with(directive, "max-stale=");
        });
    if (it != directives.end()) {
      // Get the value of the max-age directive
      std::string max_stale_str = it->substr(std::string("max-stale=").size());
      max_stale_sec = std::stoi(max_stale_str);
    }

    if (min_fresh_sec != -1) {
      if (cr.expiration_time - std::chrono::steady_clock::now() >
          std::chrono::seconds(min_fresh_sec)) {
        return "valid";
      }
      else {
        if (cr.expiration_time > std::chrono::steady_clock::now()) {
          return "must-revalidate";
        }
        else {
          return "expired";
        }
      }
    }
    if (max_stale_sec != -1) {
      if (std::chrono::steady_clock::now() - cr.expiration_time <
          std::chrono::seconds(max_stale_sec)) {
        return "valid";
      }
      else {
        return "expired";
      }
    }
    if (cr.expiration_time > std::chrono::steady_clock::now()) {
      return "valid";
    }
    else {
      return "expired";
    }
}