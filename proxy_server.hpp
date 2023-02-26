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
  http::request<http::empty_body> req_;
  http::response<http::empty_body> res_;
  LogWriter lw_;
  //tcp::resolver server_resolver_;
  bool connecting = false;

  void fail(beast::error_code ec, char const * what) {
    connecting = false;
    do_close();
    std::cerr << what << ": " << ec.message() << "\n";
  }
  void do_close() {
    // Send a TCP shutdown
    beast::error_code ec;
    client_.socket().shutdown(tcp::socket::shutdown_send, ec);
    server_.socket().shutdown(tcp::socket::shutdown_send, ec);
    // At this point the connection is closed gracefully
  }

 public:
  // Take ownership of the stream
  session(tcp::socket && socket, int id, std::ofstream & logfile) :
      client_(std::move(socket)), server_(socket.get_executor()), lw_(id, logfile) {}

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

  void on_connect_request(boost::system::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);
    if (ec.failed()) {
      return fail(ec, "on connect request");
    }
    //we receive client request here, then we need to log the request
    std::string client_addr = client_.socket().remote_endpoint().address().to_string();
    lw_.write_log(req_, client_addr);
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

  void handle_get_request() { ; }
  void handle_post_request() { ; }

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
      std::make_shared<session>(std::move(socket), num_of_session++, logfile)->run();
    }
    do_accept();

    // Accept another connection
  }
};
