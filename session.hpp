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

  // Start the asynchronous operation
  void run() {
    // We need to be executing within a strand to perform async operations
    // on the I/O objects in this session. Although not strictly necessary
    // for single-threaded contexts, this example code is written to be
    // thread-safe by default.
    client_.expires_after(std::chrono::seconds(15));
    http::async_read(
        client_,
        lead_in_,
        req_,
        beast::bind_front_handler(&session::on_connect_request, shared_from_this()));
  }

 private:
  void on_connect_request(boost::system::error_code ec, std::size_t bytes_transferred) {
    handle_IO(ec, bytes_transferred, "on connect request");
    //we receive client request here, then we need to log the request
    std::string client_addr = client_.socket().remote_endpoint().address().to_string();
    lw_.log_request_from_client(req_, client_addr);
    // std::cout << "Request: " << req_ << std::endl;

    std::pair<std::string, std::string> server_name = hp.get_server_name(req_);
    host = server_name.first;
    port = server_name.second;

    // std::cout << host << "::::" << port << std::endl;
    try {
      auto eps = tcp::resolver(server_.get_executor()).resolve(host, port);
      server_.async_connect(
          eps, beast::bind_front_handler(&session::on_connect, shared_from_this()));
    }
    catch (std::exception & e) {
      std::cerr << "fail to connect to " << host << "::::" << port << std::endl;
      std::cerr << e.what() << std::endl;
    }
  }

  void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
    if (ec) {
      // may need to send back bad response to client
      return fail(ec, "on connect");
    }

    /***
	 * here connection to server has been built
	*/
    // std::cout << "Connected to " << server_.socket().remote_endpoint() << std::endl;
    server_.expires_after(std::chrono::seconds(15));
    if (req_.method() == http::verb::connect) {
      //other method to do
      handle_connect_request();
    }
    else if (req_.method() == http::verb::get) {
      handle_get_request();
    }
    else if (req_.method() == http::verb::post) {
      handle_post_request();
    }
  }

  void handle_connect_request() {
    res_ = {http::status::ok, req_.version()};
    res_.keep_alive(true);
    res_.prepare_payload();
    auto self = shared_from_this();
    http::async_write(
        client_,
        res_,
        boost::bind(&session::on_connect_response, shared_from_this(), ph::error));
  }

  http::response<http::string_body> get_cached_response(
      const http::request<http::string_body> & req) {
    std::string cache_key = hp.get_cache_key(req);
    CachedResponse cache_value = cache_handler.get(cache_key);
    http::response<http::string_body> res = hp.parse_cachedResponse(cache_value);
    return res;
  }

  void handle_get_request() {
    // Check if there is cache in log
    std::string key = hp.get_cache_key(req_);
    CachedResponse cached_res = cache_handler.get(key);
    if (cached_res != CachedResponse()) {  //cache has reaponse
      if (cache_handler.cached_response_state(cached_res, req_) == "valid") {
        // log: ID: in cache, valid
        lw_.log_valid();
        http::response<http::string_body> resp = hp.parse_cachedResponse(cached_res);
        return http::async_write(
            client_,
            resp,
            beast::bind_front_handler(&session::get_on_write_client, shared_from_this()));
      }
      else if (cache_handler.cached_response_state(cached_res, req_) == "expired") {
        // log: ID: in cache, but expired at EXPIREDTIME
        lw_.log_expired(cached_res.get_expiration_time());
        cache_handler.remove(key);
        return http::async_write(
            server_,
            req_,
            beast::bind_front_handler(&session::get_on_write_server, shared_from_this()));
      }
      else if (cache_handler.cached_response_state(cached_res, req_) ==
               "must-revalidate") {
        // log: ID: in cache, requires validation
        lw_.log_require_validation();
        http::request<http::string_body> request{http::verb::get, "/", 11};
        request.set(http::field::host, cached_res.server);
        request.set(http::field::if_none_match, cached_res.e_tag);
        return http::async_write(
            server_,
            request,
            beast::bind_front_handler(&session::get_on_write_server, shared_from_this()));
      }
    }
    else {
      lw_.log_not_in_cache();

      http::async_write(
          server_,
          req_,
          beast::bind_front_handler(&session::get_on_write_server, shared_from_this()));
    }
  }

  void get_on_write_server(beast::error_code ec, std::size_t bytes_transferred) {
    handle_IO(ec, bytes_transferred, "get on write server");
    // log: ID: Requesting "REQUEST" from SERVER
    lw_.log_request_to_server(req_, host);
    http::async_read(
        server_,
        lead_in_,
        res_,
        beast::bind_front_handler(&session::get_on_read_server, shared_from_this()));
  }

  void get_on_read_server(beast::error_code ec, std::size_t bytes_transferred) {
    handle_IO(ec, bytes_transferred, "get on read server");
    // log: ID: Received "RESPONSE" from SERVER
    lw_.log_response_from_server(res_, host);
    if (res_.result() == http::status::not_modified) {
      return http::async_write(
          client_,
          get_cached_response(req_),
          beast::bind_front_handler(&session::get_on_write_client, shared_from_this()));
    }
    else if (res_.result() == http::status::ok) {
      // Save cache here
      if (cache_handler.can_be_cached(res_)) {
        std::string cache_key = hp.get_cache_key(req_);
        CachedResponse cache_value = hp.parse_response(res_);
        cache_handler.cache_response(cache_key, cache_value);
        lw_.log_cached_with_expire_time(cache_value.get_expiration_time());
      }
    }
    http::async_write(
        client_,
        res_,
        beast::bind_front_handler(&session::get_on_write_client, shared_from_this()));
  }

  void get_on_write_client(beast::error_code ec, std::size_t bytes_transferred) {
    handle_IO(ec, bytes_transferred, "get on write client");
    // log: ID: Responding "RESPONSE"
    lw_.log_response_to_client(res_);
    // log tunnel closed in do_close()
    lw_.log_tunnel_closed();
    do_close();
  }

  void handle_post_request() {
    http::async_write(
        server_,
        req_,
        beast::bind_front_handler(&session::post_on_write_server, shared_from_this()));
  }

  void post_on_write_server(beast::error_code ec, std::size_t bytes_transferred) {
    handle_IO(ec, bytes_transferred, "post on write server");
    // log: ID: Requesting "REQUEST" from SERVER
    lw_.log_request_to_server(req_, host);
    http::async_read(
        server_,
        lead_in_,
        res_,
        beast::bind_front_handler(&session::post_on_read_server, shared_from_this()));
  }

  void post_on_read_server(beast::error_code ec, std::size_t bytes_transferred) {
    handle_IO(ec, bytes_transferred, "post on read server");
    // log: ID: Received "RESPONSE" from SERVER
    lw_.log_response_from_server(res_, host);
    http::async_write(
        client_,
        res_,
        beast::bind_front_handler(&session::post_on_write_client, shared_from_this()));
  }

  void post_on_write_client(beast::error_code ec, std::size_t bytes_transferred) {
    handle_IO(ec, bytes_transferred, "get on write client");
    // log: ID: Responding "RESPONSE"
    lw_.log_response_to_client(res_);
    // log tunnel closed in do_close()
    lw_.log_tunnel_closed();
    do_close();
  }

  // void on_connect_response(boost::system::error_code ec, std::size_t bytes_transferred) {
  //   handle_IO(ec, bytes_transferred, "on connect response");
  //   handle_client_server_IO();

  void on_connect_response(boost::system::error_code ec) {
    if (!ec) {
      client_do_read();
      server_do_read();
    }
  }
  void client_do_read() {
    client_.socket().async_read_some(boost::asio::buffer(client_buf_),
                                     boost::bind(&session::client_on_read,
                                                 shared_from_this(),
                                                 ph::error,
                                                 ph::bytes_transferred));
  }
  void client_on_read(boost::system::error_code ec, size_t xfer) {
    if (!ec)
      async_write(
          server_.socket(),
          boost::asio::buffer(client_buf_, xfer),
          boost::bind(&session::client_on_written, shared_from_this(), ph::error));
  }
  void client_on_written(boost::system::error_code ec) {
    if (!ec)
      client_do_read();
  }
  void server_do_read() {
    server_.socket().async_read_some(boost::asio::buffer(server_buf_),
                                     boost::bind(&session::server_on_read,
                                                 shared_from_this(),
                                                 ph::error,
                                                 ph::bytes_transferred));
  }
  void server_on_read(boost::system::error_code ec, size_t xfer) {
    if (!ec)
      async_write(
          client_.socket(),
          boost::asio::buffer(server_buf_, xfer),
          boost::bind(&session::server_on_written, shared_from_this(), ph::error));
  }
  void server_on_written(boost::system::error_code ec) {
    if (!ec)
      server_do_read();
  }

  void fail(beast::error_code ec, char const * what) {
    std::cerr << what << ": " << ec.message() << "\n";
    do_close();
  }
  void do_close() {
    // Send a TCP shutdown
    beast::error_code ec;
    std::cout << "Closing conection..." << std::endl;
    // log: ID: Tunnel closed
    client_.socket().shutdown(tcp::socket::shutdown_send, ec);
    server_.socket().shutdown(tcp::socket::shutdown_send, ec);
    // At this point the connection is closed gracefully
  }
  void handle_IO(beast::error_code ec, std::size_t bytes_transferred, char const * what) {
    boost::ignore_unused(bytes_transferred);
    if (ec) {
      return fail(ec, what);
    }
  }
};
#endif  //SESSION