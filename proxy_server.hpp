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
  beast::tcp_stream client_;
	beast::tcp_stream server_;
  net::streambuf lead_in_;
  http::request<http::empty_body> req_;
	http::response<http::empty_body> res_;
  LogWriter & lw_;
	//tcp::resolver server_resolver_;
	bool connecting = false;

	void send_ok_response(beast::tcp_stream &client_stream){
    std::string response = "HTTP/1.1 200 OK\r\n\r\n";
  	boost::asio::write(client_, boost::asio::buffer(response));
}
public:
  // Take ownership of the stream
	session(tcp::socket&& socket, LogWriter& lw)
  : client_(std::move(socket)), 
		server_(socket.get_executor()),
		lw_(lw) {}

  // Start the asynchronous operation
  void run() {
    // We need to be executing within a strand to perform async operations
    // on the I/O objects in this session. Although not strictly necessary
    // for single-threaded contexts, this example code is written to be
    // thread-safe by default.
    http::async_read(client_, lead_in_, req_, 
										beast::bind_front_handler(&session::on_connect_request, shared_from_this()));
  }

	void on_connect_request(boost::system::error_code ec, std::size_t bytes_transferred) {
		if (ec.failed() || req_.method() != http::verb::connect){
			return fail(ec, "on connect request");
		}
		std::cout << "Connect request: " << req_ << std::endl;
		std::string upstream(req_.target());
		std::string host;
		std::string port = "80"; // default port number is 80 for HTTP
		std::size_t colon_pos = upstream.find(":");
		if (colon_pos != std::string::npos) {
				port = upstream.substr(colon_pos + 1);
				host = upstream.substr(0, colon_pos);
		}
		auto eps = tcp::resolver(server_.get_executor()).resolve(host, port);
		server_.async_connect(eps,
												beast::bind_front_handler(&session::on_connect, shared_from_this()));
	}

	void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type){
		if (ec) {
			// may need to send back bad response to client
			return fail(ec, "on connect");
		}
		std::cout << "Connected to " << server_.socket().remote_endpoint() << std::endl;
		server_.expires_after(std::chrono::seconds(30));
		res_ = {http::status::ok, req_.version()};
		res_.keep_alive(true);
		res_.prepare_payload();
		http::async_write(client_, res_,
											beast::bind_front_handler(&session::on_connect_response, shared_from_this()));
	}

	void on_connect_response(boost::system::error_code ec, std::size_t bytes_transferred){
		if (ec) {
			return fail(ec, "on connect response");
		}
		// http::async_read(client_, lead_in_, req_, 
		// 								beast::bind_front_handler(&session::on_connect_request, shared_from_this()));
	}

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

  void do_close() {
    // Send a TCP shutdown
    beast::error_code ec;
    client_.socket().shutdown(tcp::socket::shutdown_send, ec);
		server_.socket().shutdown(tcp::socket::shutdown_send, ec);
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
