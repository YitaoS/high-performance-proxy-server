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
#include "request_handler.hpp"

namespace beast = boost::beast;    // from <boost/beast.hpp>
namespace http = beast::http;      // from <boost/beast/http.hpp>
namespace net = boost::asio;       // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;  // from <boost/asio/ip/tcp.hpp>
using socket_type = tcp::socket;



class session : public std::enable_shared_from_this<session> {
  beast::tcp_stream client_;
  beast::tcp_stream server_;
  net::streambuf lead_in_;
  http::request<http::empty_body> req_;
  http::response<http::empty_body> res_;
  LogWriter lw_;
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
  std::string check_state(http::response<http::string_body> res){
    return "11";
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
    handle_IO(ec, bytes_transferred, "on connect request");
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
    http::async_write(
        client_,
        res_,
        beast::bind_front_handler(&session::on_connect_response, shared_from_this()));
  }

  void handle_get_request() {
    // Check if there is cache in log
    std::optional<http::response<http::string_body> > cache_res; // = cache_response(req_)
    if (cache_res.has_value()){
      if (check_state(*cache_res) == "valid"){
        // log: ID: in cache, valid
        http::async_write(
              client_,
              *cache_res,
              beast::bind_front_handler(&session::get_on_write_client, shared_from_this()));
      }
      else if (check_state(*cache_res) == "expired"){
        ;// log: ID: in cache, but expired at EXPIREDTIME
      }
      else if (check_state(*cache_res) == "requires validation"){
        ;// log: ID: in cache, requires validation
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
    if (res_.result() == http::status::ok){
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

  void on_connect_response(boost::system::error_code ec, std::size_t bytes_transferred) {
    handle_IO(ec, bytes_transferred, "on connect response");
    handle_client_server_IO();
  }

  void handle_client_server_IO() {
    if (req_.method() == http::verb::get || req_.method() == http::verb::post) {
      if (connecting) {
        http::async_read(
            client_,
            lead_in_,
            req_,
            beast::bind_front_handler(&session::on_read_client, shared_from_this()));
      }
      else {
        ////// CAN SEARCH CACHE HERE ////////
        http::async_write(
            server_,
            req_,
            beast::bind_front_handler(&session::on_write_server, shared_from_this()));
      }
    }

    if (req_.method() == http::verb::connect) {
    }
  }

  void on_read_client(beast::error_code ec, std::size_t bytes_transferred) {
    handle_IO(ec, bytes_transferred, "on read client");
    std::cout << "read from client: " << req_ << std::endl;
    http::async_write(
        server_,
        req_,
        beast::bind_front_handler(&session::on_write_server, shared_from_this()));
  }

  void on_write_server(beast::error_code ec, std::size_t bytes_transferred) {
    handle_IO(ec, bytes_transferred, "on write server");
    http::async_read(
        server_,
        lead_in_,
        res_,
        beast::bind_front_handler(&session::on_read_server, shared_from_this()));
  }

  void on_read_server(beast::error_code ec, std::size_t bytes_transferred) {
    handle_IO(ec, bytes_transferred, "on read server");
    if (!connecting) {  //// CAN SAVE CACHE HERE ////}
      http::async_write(
          client_,
          res_,
          beast::bind_front_handler(&session::on_write_client, shared_from_this()));
    }
  }

  void on_write_client(beast::error_code ec, std::size_t bytes_transferred) {
    handle_IO(ec, bytes_transferred, "on write client");
    if (connecting) {
      on_connect(ec, server_.socket().remote_endpoint());
    }
  }
};

// void do_read() {
//   // Make the request empty before reading,
//   // otherwise the operation behavior is undefined.
//   req_ = {};

//   // Set the timeout.
//   stream_.expires_after(std::chrono::seconds(30));

//   // Read a request
//   http::async_read(stream_,
//                    buffer_,
//                    req_,
//                    beast::bind_front_handler(&session::on_read, shared_from_this()));
// }

// void on_read(beast::error_code ec, std::size_t bytes_transferred) {
//   boost::ignore_unused(bytes_transferred);

//   // This means they closed the connection
//   if (ec == http::error::end_of_stream)
//     return do_close();

//   if (ec)
//     return fail(ec, "read");

//   // write the log file
//   std::string client_addr = stream_.socket().remote_endpoint().address().to_string();
//   lw_.write_log(std::move(req_), client_addr);
//   handle_request(req_);
// }

// void handle_request(http::request<http::string_body> server_req){
// 	// If bad request send response to user.
// 	// Extract the hostname and port from the request object
// 	beast::string_view host_str = server_req.base().at("Host");
// 	std::string host = host_str.to_string();
// 	std::string port = "80"; // default port number is 80 for HTTP
// 	std::size_t colon_pos = host.find(":");
// 	if (colon_pos != std::string::npos) {
// 			port = host.substr(colon_pos + 1);
// 			host = host.substr(0, colon_pos);
// 	}
// 	std::cout<<host<<","<<port<<std::endl;
// 	connect_server(host.c_str(), port.c_str());
// }

// void connect_server(char const* host, char const* port){
// 	std::cout<<"connect server"<<std::endl;
// server_resolver_.async_resolve(
//     host,
//     port,
//     beast::bind_front_handler(
//         &session::server_on_resolve,
//         shared_from_this()
//     )
// );
//}

// void server_on_resolve(beast::error_code ec, tcp::resolver::results_type results){
//   if (ec){
//       return fail(ec, "resolve");
//   }
//   server_stream_.async_connect(
//       results,
//       beast::bind_front_handler(&session::server_on_connect, shared_from_this())
//   );
// }

// void server_on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type){
// 	if(ec){
// 			return fail(ec, "connect");
// 	}
// 	// Set a timeout on the operation
// 	server_stream_.expires_after(std::chrono::seconds(30));
// 	// send client an ok response if the request is CONNECT
// 	if (req_.method() == http::verb::connect){
// 			connecting = true;
// 			send_ok_response(stream_);
// 			// call back to listening to client request
// 	}
// 	// Send the HTTP request to the remote host
// 	http::async_write(server_stream_, req_,
// 			beast::bind_front_handler(
// 					&session::server_on_write,
// 					shared_from_this()));
// }

// void server_on_write(beast::error_code ec, std::size_t bytes_transferred){
//   boost::ignore_unused(bytes_transferred);
//   if(ec){
//       return fail(ec, "write");
//   }
//   // Receive the HTTP response
//   http::async_read(server_stream_, server_buffer_, res_,
//       beast::bind_front_handler(
//           &session::server_on_read,
//           shared_from_this()));
// }

// void server_on_read(beast::error_code ec, std::size_t bytes_transferred){
// 	boost::ignore_unused(bytes_transferred);
// 	if (ec){
// 			return fail(ec, "read");
// 	}
// 	// Write the message to standard out
// 	//// log & cache
// 	std::cout << res_ << std::endl;
// 	// response to the client
// 	//send_response_to_client(client_stream_);
// }

// void send_response(http::message_generator && msg) {
//   bool keep_alive = msg.keep_alive();

//   // Write the response
//   http::async_write(
//       stream_,
//       std::move(msg),
//       beast::bind_front_handler(&session::on_write, shared_from_this(), keep_alive));
// }

// void on_write(bool keep_alive, beast::error_code ec, std::size_t bytes_transferred) {
//   boost::ignore_unused(bytes_transferred);

//   if (ec)
//     return fail(ec, "write");

//   if (!keep_alive) {
//     // This means we should close the connection, usually because
//     // the response indicated the "Connection: close" semantic.
//     return do_close();
//   }

//   // Read another request
//   do_read();
// }

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
