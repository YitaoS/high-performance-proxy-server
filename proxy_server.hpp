#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast.hpp>
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

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
#include "cache.hpp"
#include "log_writer.hpp"
#include "request_handler.hpp"

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
  Cache<std::string, CachedResponse> & http_cache;
  //tcp::resolver server_resolver_;
  bool connecting = false;

  void fail(beast::error_code ec, char const * what) {
    connecting = false;
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
  std::string check_state(http::response<http::string_body> res) { return "11"; }

 public:
  // Take ownership of the stream
  session(tcp::socket && socket,
          int id,
          std::ofstream & logfile,
          Cache<std::string, CachedResponse> & cache) :
      client_(std::move(socket)),
      server_(socket.get_executor()),
      lw_(id, logfile),
      http_cache(cache) {}

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
    std::cout << "Request: " << req_ << std::endl;
    std::string upstream(req_.target());
    std::string host;
    std::string port = "80";  // default port number is 80 for HTTP
    std::size_t colon_pos = upstream.find(":");
    if (colon_pos != std::string::npos) {
      port = upstream.substr(colon_pos + 1);
      host = upstream.substr(0, colon_pos);
    }
    auto eps = tcp::resolver(server_.get_executor()).resolve(host, port);
    server_.async_connect(
        eps, beast::bind_front_handler(&session::on_connect, shared_from_this()));
  }

  void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
    if (ec) {
      // may need to send back bad response to client
      return fail(ec, "on connect");
    }

    /***
	 * here connection to server has been built
	*/
    std::cout << "Connected to " << server_.socket().remote_endpoint() << std::endl;
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

  std::string get_cache_key(const http::request<http::string_body> & req) {
    std::string cache_key = req.method_string();
    cache_key.append(req.target().data(), req.target().size());
    return cache_key;
  }
  CachedResponse parse_response(const http::response<http::string_body> & resp) {
    CachedResponse cached_resp;
    //store status_code
    cached_resp.status_code = resp.result_int();
    //store the message
    std::string status_message = std::string(resp.reason());
    //store body
    cached_resp.body = resp.body();
    //store e-tag
    const auto & headers = resp.base();
    auto it = headers.find(boost::beast::http::field::etag);
    if (it != headers.end()) {
      cached_resp.e_tag = it->value();
      // use the e_tag value as needed
    }
    //store content type
    auto content_type = resp.find(http::field::content_type);
    if (content_type != resp.end()) {
      cached_resp.content_type = content_type->value();
    }
    else {
      cached_resp.content_type = "";
    }
    //store server
    auto server = resp.find(http::field::server);
    if (content_type != resp.end()) {
      cached_resp.server = server->value();
    }
    else {
      cached_resp.server = "";
    }
    //store fresh time and expiration time
    const auto & headers = resp.base();
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
        cached_resp.fresh_time = std::chrono::steady_clock::now() + max_age;
        auto it = std::find_if(
            directives.begin(), directives.end(), [](const std::string & directive) {
              return directive == "must-revalidate";
            });

        if (it != directives.end()) {
          // The response has the must-revalidate directive
          cached_resp.must_revalidate = true;
        }
        else {
          cached_resp.must_revalidate = false;
          auto it = std::find_if(
              directives.begin(), directives.end(), [](const std::string & directive) {
                return boost::starts_with(directive, "max-stale=");
              });
          if (it != directives.end()) {
            // Get the value of the max-age directive
            std::string max_stale_str = it->substr(std::string("max-stale=").size());
            int max_stale_sec = std::stoi(max_stale_str);

            // Calculate the maximum age in seconds
            std::chrono::seconds max_stale(max_stale_sec);

            // Calculate the expiration time of the response
            cached_resp.expiration_time = std::chrono::steady_clock::now() + max_stale;
          }
        }
      }
    }
    return cached_resp;
  }
  void cache_response(const http::request<http::string_body> & req,
                      const http::response<http::string_body> & resp) {
    std::string cache_key = get_cache_key(req);
    CachedResponse cache_value = parse_response(resp);
    http_cache.put(cache_key, cache_value);
  }
  bool can_be_cached(const http::response<http::string_body> & resp) {
    const auto & headers = resp.base();
    if (resp.chunked() == true) {
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
            return directive == "no-cache";
          });

      if (it != directives.end()) {
        // The response has the must-revalidate directive
        return false;
      }
      auto it = std::find_if(
          directives.begin(), directives.end(), [](const std::string & directive) {
            return directive == "no-store";
          });

      if (it != directives.end()) {
        // The response has the must-revalidate directive
        return false;
      }
      auto it = std::find_if(
          directives.begin(), directives.end(), [](const std::string & directive) {
            return directive == "private";
          });

      if (it != directives.end()) {
        // The response has the must-revalidate directive
        return false;
      }
      return true;
    }
    else {
      return false;
    }
  }
  std::string cached_response_state(const CachedResponse & cr) {
    if (std::chrono::steady_clock::now() < cr.fresh_time) {
      return "valid";
    }
    else if (std::chrono::steady_clock::now() < cr.expiration_time) {
      if (cr.must_revalidate == true) {
        return "must-revalidate";
      }
      else {
        return "valid";
      }
    }
    else {
      return "invalid";
    }
  }
  void handle_get_request() {
    // Check if there is cache in log
    std::string key = get_cache_key(req_);
    CachedResponse response = http_cache.get(key);
    if (response != CachedResponse()) {  //cache has reaponse
      if (cached_response_state(response) == "valid") {
        // log: ID: in cache, valid
        http::response<http::string_body> resp;

        http::async_write(
            client_,
            *cache_res,
            beast::bind_front_handler(&session::get_on_write_client, shared_from_this()));
      }
      else if (cached_response_state(response) == "expired") {
        ;  // log: ID: in cache, but expired at EXPIREDTIME
      }
      else if (cached_response_state(response) == "must-revalidate") {
        ;  // log: ID: in cache, requires validation
      }
    }
    //// ElSE IS NEEDED HERE! /////
    // else {
    //   log: ID: not in cache
    // }
    http::async_write(
        server_,
        req_,
        beast::bind_front_handler(&session::get_on_write_server, shared_from_this()));
  }

  void get_on_write_server(beast::error_code ec, std::size_t bytes_transferred) {
    handle_IO(ec, bytes_transferred, "get on write server");
    // log: ID: Requesting "REQUEST" from SERVER
    http::async_read(
        server_,
        lead_in_,
        res_,
        beast::bind_front_handler(&session::get_on_read_server, shared_from_this()));
  }

  void get_on_read_server(beast::error_code ec, std::size_t bytes_transferred) {
    handle_IO(ec, bytes_transferred, "get on read server");
    // log: ID: Received "RESPONSE" from SERVER
    if (res_.result() == http::status::ok) {
      // log: ID: not cacheable because REASON
      // ID: cached, expires at EXPIRES
      // ID: cached, but requires re-validation
      // Save cache here
      // cache_response(req_, res_);
    }
    http::async_write(
        client_,
        res_,
        beast::bind_front_handler(&session::get_on_write_client, shared_from_this()));
  }

  void get_on_write_client(beast::error_code ec, std::size_t bytes_transferred) {
    handle_IO(ec, bytes_transferred, "get on write client");
    // log: ID: Responding "RESPONSE"
    // log tunnel closed in do_close()
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
    http::async_read(
        server_,
        lead_in_,
        res_,
        beast::bind_front_handler(&session::post_on_read_server, shared_from_this()));
  }

  void post_on_read_server(beast::error_code ec, std::size_t bytes_transferred) {
    handle_IO(ec, bytes_transferred, "post on read server");
    // log: ID: Received "RESPONSE" from SERVER
    http::async_write(
        client_,
        res_,
        beast::bind_front_handler(&session::post_on_write_client, shared_from_this()));
  }

  void post_on_write_client(beast::error_code ec, std::size_t bytes_transferred) {
    handle_IO(ec, bytes_transferred, "get on write client");
    // log: ID: Responding "RESPONSE"
    // log tunnel closed in do_close()
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

  void on_connect_response(boost::system::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);
    if (ec) {
      return fail(ec, "on connect response");
    }
  }
};

class listener : public std::enable_shared_from_this<listener> {
  net::io_context & ioc_;
  tcp::acceptor acceptor_;
  std::ofstream & logfile;
  Cache<std::string, CachedResponse> http_cache;
  int num_of_session;
  //std::shared_ptr<std::string const> doc_root_;

  void fail(beast::error_code ec, char const * what) {
    std::cerr << what << ": " << ec.message() << "\n";
  }

 public:
  listener(net::io_context & ioc,
           tcp::endpoint endpoint,
           std::ofstream & logfile,
           int capacity) :
      ioc_(ioc),
      acceptor_(net::make_strand(ioc)),
      logfile(logfile),
      http_cache(capacity),
      num_of_session(0) {
    beast::error_code ec;

    // Open the acceptor
    acceptor_.open(endpoint.protocol(), ec);
    if (ec) {
      fail(ec, "open");
      return;
    }

    // Allow address reuse
    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec) {
      fail(ec, "set_option");
      return;
    }

    // Bind to the server address
    acceptor_.bind(endpoint, ec);
    if (ec) {
      fail(ec, "bind");
      return;
    }

    // Start listening for connections
    acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec) {
      fail(ec, "listen");
      return;
    }
  }

  // Start accepting incoming connections
  void run() { do_accept(); }

 private:
  void do_accept() {
    // The new connection gets its own strand
    acceptor_.async_accept(
        net::make_strand(ioc_),
        beast::bind_front_handler(&listener::on_accept, shared_from_this()));
  }

  void on_accept(beast::error_code ec, tcp::socket socket) {
    if (ec) {
      fail(ec, "accept");
      return;  // To avoid infinite loop
    }
    else {
      // Create the session and run it
      std::make_shared<session>(std::move(socket), num_of_session++, logfile, http_cache)
          ->run();
    }
    do_accept();

    // Accept another connection
  }
};
