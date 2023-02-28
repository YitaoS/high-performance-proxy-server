#ifndef SESSION
#define SESSION
#include <boost/asio.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/bind/bind.hpp>
#include <boost/config.hpp>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cache.hpp"
#include "cache_handler.hpp"
#include "http_parser.hpp"
#include "log_writer.hpp"

namespace beast = boost::beast;  // from <boost/beast.hpp>
namespace http = beast::http;    // from <boost/beast/http.hpp>
namespace net = boost::asio;     // from <boost/asio.hpp>
namespace ph = boost::asio::placeholders;
using tcp = boost::asio::ip::tcp;  // from <boost/asio/ip/tcp.hpp>

class session : public std::enable_shared_from_this<session> {
  beast::tcp_stream client_;
  beast::tcp_stream server_;
  net::streambuf lead_in_;
  std::array<uint8_t, 8192> client_buf_;
  std::array<uint8_t, 8192> server_buf_;
  http::request<http::string_body> req_;
  http::response<http::string_body> res_;
  LogWriter lw_;
  CacheHandler cache_handler;
  std::string host;
  std::string port;
  HttpParser hp;

 public:
  // Take ownership of the stream
  session(tcp::socket && socket,
          int id,
          std::ofstream & logfile,
          Cache<std::string, CachedResponse> & cache,
          std::mutex & mutex) :
      client_(std::move(socket)),
      server_(socket.get_executor()),
      lw_(id, logfile, mutex),
      cache_handler(cache, lw_) {}

  void run();

 private:
  void on_connect_request(boost::system::error_code ec, std::size_t bytes_transferred);

  void on_write_bad_client(beast::error_code ec, std::size_t bytes_transferred);

  void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type);

  void handle_connect_request();

  http::response<http::string_body> get_cached_response(
      const http::request<http::string_body> & req);

  void handle_get_request();

  void get_on_write_server(beast::error_code ec, std::size_t bytes_transferred);

  void get_on_read_server(beast::error_code ec, std::size_t bytes_transferred);

  void get_on_write_client(beast::error_code ec, std::size_t bytes_transferred);

  void handle_post_request();

  void post_on_write_server(beast::error_code ec, std::size_t bytes_transferred);

  void post_on_read_server(beast::error_code ec, std::size_t bytes_transferred);

  void post_on_write_client(beast::error_code ec, std::size_t bytes_transferred);

  void on_connect_response(beast::error_code ec, std::size_t bytes_transferred);

  void client_do_read();

  void client_on_read(beast::error_code ec, std::size_t bytes_transferred);

  void client_on_written(beast::error_code ec, std::size_t bytes_transferred);
  void server_do_read();

  void server_on_read(beast::error_code ec, std::size_t bytes_transferred);

  void server_on_written(beast::error_code ec, std::size_t bytes_transferred);

  void fail(beast::error_code ec, char const * what) { do_close(); }

  void do_close();

  void check_error(beast::error_code ec,
                   std::size_t bytes_transferred,
                   char const * what);

  void send_bad_response(http::status status, std::string body);
};
#endif  //SESSION