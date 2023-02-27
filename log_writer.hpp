#include <boost/asio.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/config.hpp>

#include <ctime>
#include <iomanip>
#include <fstream>
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
  std::mutex log_mutex;
  // Returns the current time as a string in UTC with asctime format
  std::string current_utc_time();
  std::string to_utc_string(const std::chrono::steady_clock::time_point & tp);

 public:
  LogWriter(int id, std::ofstream & log) : id(id), logfile(log) {}

  template<class Body, class Allocator>
  void log_request_from_client(
      const http::request<Body, http::basic_fields<Allocator> > & request,
      std::string client_address) {
    std::lock_guard<std::mutex> lock(log_mutex);
    logfile << id << ": \"" << request.method_string() << " " << request.target() << " "
            << "HTTP/" << request.version() << "\" from " << client_address << " @ "
            << current_utc_time() << std::endl;
  }

  template<class Body, class Allocator>
  void log_request_to_server(
      const http::request<Body, http::basic_fields<Allocator> > & request,
      std::string server_address) {
    std::lock_guard<std::mutex> lock(log_mutex);
    logfile << id << ": Requesting \"" << request.method_string() << " "
            << request.target() << " "
            << "HTTP/" << request.version() << "\" from ";
    const auto & server_field = request.find("Server");
    if (server_field != request.end()) {
      logfile << server_field->value() << std::endl;
    }
  }

  void log_response_from_server(const http::response<http::string_body> & response) {
    std::lock_guard<std::mutex> lock(log_mutex);
    logfile << id << ": Receiving \""
            << "HTTP/" << response.version() << " " << response.result_int() << " "
            << response.reason() << "\" from ";
    const auto & server_field = response.find("Server");
    if (server_field != response.end()) {
      logfile << server_field->value() << std::endl;
    }
  }

  void log_response_to_client(const http::response<http::string_body> & response) {
    std::lock_guard<std::mutex> lock(log_mutex);
    logfile << id << ": Responding \""
            << "HTTP/" << response.version() << " " << response.result_int() << " "
            << response.reason() << "\"" << std::endl;
  }

  void log_tunnel_closed() {
    std::lock_guard<std::mutex> lock(log_mutex);
    logfile << id << ": Tunnel closed" << std::endl;
  }

  void log_note(std::string note) {
    std::lock_guard<std::mutex> lock(log_mutex);
    logfile << id << ": NOTE " << note << std::endl;
  }

  void log_warning(std::string warning) {
    std::lock_guard<std::mutex> lock(log_mutex);
    logfile << id << ": WARNING " << warning << std::endl;
  }

  void log_error(std::string error) {
    std::lock_guard<std::mutex> lock(log_mutex);
    logfile << id << ": WARNING " << error << std::endl;
  }

  //method while handling get req from client
  void log_not_in_cache() {
    std::lock_guard<std::mutex> lock(log_mutex);
    logfile << id << ": not in cache" << std::endl;
  }
  void log_expired(std::chrono::steady_clock::time_point time) {
    std::lock_guard<std::mutex> lock(log_mutex);
    logfile << id << ": in cache, but expired at " << to_utc_string(time) << std::endl;
  }
  void log_require_validation() {
    std::lock_guard<std::mutex> lock(log_mutex);
    logfile << id << ": in cache, requires validation" << std::endl;
  }
  void log_valid() {
    std::lock_guard<std::mutex> lock(log_mutex);
    logfile << id << ": in cache, valid" << std::endl;
  }

  //method while handling get resp from server
  void log_not_cacheable(std::string reason) {
    std::lock_guard<std::mutex> lock(log_mutex);
    logfile << id << ": not cacheable because " << reason << std::endl;
  }
  void log_cached_with_expire_time(std::chrono::steady_clock::time_point time) {
    std::lock_guard<std::mutex> lock(log_mutex);
    logfile << id << ": cached, expires at " << to_utc_string(time) << std::endl;
  }
  void log_cached_with_revalidation() {
    std::lock_guard<std::mutex> lock(log_mutex);
    logfile << id << ": cached, but requires re-validation" << std::endl;
  }
};