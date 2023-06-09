#ifndef LOG_WRITER
#define LOG_WRITER

#include <boost/asio.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/config.hpp>

#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>

namespace beast = boost::beast;    // from <boost/beast.hpp>
namespace http = beast::http;      // from <boost/beast/http.hpp>
namespace net = boost::asio;       // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;  // from <boost/asio/ip/tcp.hpp>

class LogWriter {
 private:
  int id;
  std::ofstream & logfile;
  std::mutex & log_mutex;
  // Returns the current time as a string in UTC with asctime format
  std::string current_utc_time();
  std::string to_utc_string(const std::chrono::steady_clock::time_point & tp);

 public:
  LogWriter(int id, std::ofstream & log, std::mutex & mutex) :
      id(id), logfile(log), log_mutex(mutex) {}

  template<class Body, class Allocator>
  void log_request_from_client(
      const http::request<Body, http::basic_fields<Allocator> > & request,
      std::string client_address) {
    std::lock_guard<std::mutex> lock(log_mutex);
    auto http_version = request.version();
    std::string http_version_string = "HTTP/" + std::to_string(http_version / 10) + "." +
                                      std::to_string(http_version % 10);
    logfile << id << ": \"" << request.method_string() << " " << request.target() << " "
            << "HTTP/" << http_version_string << "\" from " << client_address << " @ "
            << current_utc_time() << std::endl;
  }

  template<class Body, class Allocator>
  void log_request_to_server(
      const http::request<Body, http::basic_fields<Allocator> > & request,
      std::string server_name) {
    std::lock_guard<std::mutex> lock(log_mutex);
    logfile << id << ": Requesting \"" << request.method_string() << " "
            << request.target() << " "
            << "HTTP/" << request.version() << "\" from " << server_name << std::endl;
  }

  void log_response_from_server(const http::response<http::string_body> & response,
                                std::string server_name);
  void log_response_to_client(const http::response<http::string_body> & response);
  void log_tunnel_closed();
  void log_note(std::string note);
  void log_warning(std::string warning);
  void log_error(std::string error);
  //method while handling get req from client
  void log_not_in_cache();
  void log_expired(std::chrono::steady_clock::time_point time);
  void log_require_validation();
  void log_valid();
  //method while handling get resp from server
  void log_not_cacheable(std::string reason);
  void log_cached_with_expire_time(std::chrono::steady_clock::time_point time);
  void log_cached_with_revalidation();
};

#endif  //LOG_WRITER