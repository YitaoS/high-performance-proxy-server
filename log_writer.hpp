#include <boost/asio.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/config.hpp>

#include <ctime>
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

 public:
  LogWriter(std::ofstream & log) : id(0), logfile(log) {}
  template<class Body, class Allocator>
  void write_log(const http::request<Body, http::basic_fields<Allocator> > && request,
                 std::string address) {
    std::lock_guard<std::mutex> lock(log_mutex);
    logfile << id << ": \"" << request.method_string() << " " << request.target() << " "
            << "HTTP/" << request.version() << "\" from " << address << " @ "
            << current_utc_time() << std::endl;
    id++;
  }
};