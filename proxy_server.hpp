#include <boost/asio.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
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

namespace beast = boost::beast;    // from <boost/beast.hpp>
namespace http = beast::http;      // from <boost/beast/http.hpp>
namespace net = boost::asio;       // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;  // from <boost/asio/ip/tcp.hpp>

void fail(beast::error_code ec, char const * what) {
  std::cerr << what << ": " << ec.message() << "\n";
}

class session : public std::enable_shared_from_this<session> {
  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;
  std::shared_ptr<std::string const> doc_root_;
  http::request<http::string_body> req_;
  LogWriter & lw_;

 public:
  // Take ownership of the stream
  session(tcp::socket && socket, LogWriter & lw_) :
      stream_(std::move(socket)), lw_(lw_) {}

  // Start the asynchronous operation
  void run() {
    // We need to be executing within a strand to perform async operations
    // on the I/O objects in this session. Although not strictly necessary
    // for single-threaded contexts, this example code is written to be
    // thread-safe by default.
    net::dispatch(stream_.get_executor(),
                  beast::bind_front_handler(&session::do_read, shared_from_this()));
  }

  void do_read() {
    // Make the request empty before reading,
    // otherwise the operation behavior is undefined.
    req_ = {};

    // Set the timeout.
    stream_.expires_after(std::chrono::seconds(30));

    // Read a request
    http::async_read(stream_,
                     buffer_,
                     req_,
                     beast::bind_front_handler(&session::on_read, shared_from_this()));
  }

  void on_read(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    // This means they closed the connection
    if (ec == http::error::end_of_stream)
      return do_close();

    if (ec)
      return fail(ec, "read");

    // write the log file

    std::string client_addr = stream_.socket().remote_endpoint().address().to_string();
    lw_.write_log(std::move(req_), client_addr);
    //handle_request(std::move(req_));
  }

  void send_response(http::message_generator && msg) {
    bool keep_alive = msg.keep_alive();

    // Write the response
    beast::async_write(
        stream_,
        std::move(msg),
        beast::bind_front_handler(&session::on_write, shared_from_this(), keep_alive));
  }

  void on_write(bool keep_alive, beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    if (ec)
      return fail(ec, "write");

    if (!keep_alive) {
      // This means we should close the connection, usually because
      // the response indicated the "Connection: close" semantic.
      return do_close();
    }

    // Read another request
    do_read();
  }

  void do_close() {
    // Send a TCP shutdown
    beast::error_code ec;
    stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

    // At this point the connection is closed gracefully
  }
};

class listener : public std::enable_shared_from_this<listener> {
  net::io_context & ioc_;
  tcp::acceptor acceptor_;
  LogWriter lw_;
  Cache<std::string, CachedResponse> http_cache;
  //std::shared_ptr<std::string const> doc_root_;

 public:
  listener(net::io_context & ioc,
           tcp::endpoint endpoint,
           std::ofstream & logfile,
           int capacity) :
      ioc_(ioc), acceptor_(net::make_strand(ioc)), lw_(logfile), http_cache(capacity) {
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
      std::make_shared<session>(std::move(socket), lw_)->run();
    }

    // Accept another connection
    do_accept();
  }
};
