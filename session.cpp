#include "session.hpp"

#include "cache_handler.hpp"

void session::run() {
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

void session::on_connect_request(boost::system::error_code ec,
                                 std::size_t bytes_transferred) {
  check_error(ec, bytes_transferred, "on connect request");
  //we receive client request here, then we need to log the request
  std::string client_addr = client_.socket().remote_endpoint().address().to_string();
  lw_.log_request_from_client(req_, client_addr);
  std::pair<std::string, std::string> server_name = hp.get_server_name(req_);
  host = server_name.first;
  port = server_name.second;
  try {
    auto eps = tcp::resolver(server_.get_executor()).resolve(host, port);
    server_.async_connect(
        eps, beast::bind_front_handler(&session::on_connect, shared_from_this()));
  }
  catch (std::exception & e) {
    send_bad_response(http::status::bad_request, "Bad Request");
  }
}

void session::on_write_bad_client(beast::error_code ec, std::size_t bytes_transferred) {
  check_error(ec, bytes_transferred, "on write bad client");
  // log: ID: Responding "RESPONSE"
  lw_.log_response_to_client(res_);
  // log tunnel closed in do_close()
  lw_.log_tunnel_closed();
  do_close();
}

void session::on_connect(beast::error_code ec,
                         tcp::resolver::results_type::endpoint_type) {
  if (ec) {
    // may need to send back bad response to client
    send_bad_response(http::status::bad_request, "Bad Request");
    return fail(ec, "on connect");
  }
  /***
	 * here connection to server has been built
	*/
  server_.expires_after(std::chrono::seconds(15));
  if (req_.method() == http::verb::connect) {
    handle_connect_request();
  }
  else if (req_.method() == http::verb::get) {
    handle_get_request();
  }
  else if (req_.method() == http::verb::post) {
    handle_post_request();
  }
  else {
    send_bad_response(http::status::bad_request, "Bad Request");
  }
}

void session::handle_connect_request() {
  res_ = {http::status::ok, req_.version()};
  res_.keep_alive(true);
  res_.prepare_payload();
  auto self = shared_from_this();
  http::async_write(
      client_,
      res_,
      beast::bind_front_handler(&session::on_connect_response, shared_from_this()));
}

http::response<http::string_body> session::get_cached_response(
    const http::request<http::string_body> & req) {
  std::string cache_key = hp.get_cache_key(req);
  CachedResponse cache_value = cache_handler.get(cache_key);
  http::response<http::string_body> res = hp.parse_cached_response(cache_value);
  return res;
}

void session::handle_get_request() {
  // Check if there is cache in log
  std::string key = hp.get_cache_key(req_);
  CachedResponse cached_res = cache_handler.get(key);
  if (cached_res != CachedResponse()) {  //cache has reaponse
    if (cache_handler.cached_response_state(cached_res, req_) == "valid") {
      // log: ID: in cache, valid
      lw_.log_valid();
      http::response<http::string_body> resp = hp.parse_cached_response(cached_res);
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
    else if (cache_handler.cached_response_state(cached_res, req_) == "must-revalidate") {
      // log: ID: in cache, requires validation
      lw_.log_require_validation();
      req_ = {http::verb::get, "/", 11};
      req_.set(http::field::host, cached_res.server);
      req_.set(http::field::if_none_match, cached_res.e_tag);
      return http::async_write(
          server_,
          req_,
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

void session::get_on_write_server(beast::error_code ec, std::size_t bytes_transferred) {
  check_error(ec, bytes_transferred, "get on write server");
  // log: ID: Requesting "REQUEST" from SERVER
  lw_.log_request_to_server(req_, host);
  http::async_read(
      server_,
      lead_in_,
      res_,
      beast::bind_front_handler(&session::get_on_read_server, shared_from_this()));
}

void session::get_on_read_server(beast::error_code ec, std::size_t bytes_transferred) {
  check_error(ec, bytes_transferred, "get on read server");
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
    }
    return http::async_write(
        client_,
        res_,
        beast::bind_front_handler(&session::get_on_write_client, shared_from_this()));
  }
  else if (res_.result() > beast::http::status::ok &&
           res_.result() < beast::http::status::multiple_choices) {
    return http::async_write(
        client_,
        res_,
        beast::bind_front_handler(&session::get_on_write_client, shared_from_this()));
  }
  else {
    send_bad_response(http::status::bad_gateway, "502 Bad Gateway");
  }
}

void session::get_on_write_client(beast::error_code ec, std::size_t bytes_transferred) {
  check_error(ec, bytes_transferred, "get on write client");
  // log: ID: Responding "RESPONSE"
  lw_.log_response_to_client(res_);
  // log tunnel closed in do_close()
  lw_.log_tunnel_closed();
  do_close();
}

void session::handle_post_request() {
  http::async_write(
      server_,
      req_,
      beast::bind_front_handler(&session::post_on_write_server, shared_from_this()));
}

void session::post_on_write_server(beast::error_code ec, std::size_t bytes_transferred) {
  check_error(ec, bytes_transferred, "post on write server");
  // log: ID: Requesting "REQUEST" from SERVER
  lw_.log_request_to_server(req_, host);
  http::async_read(
      server_,
      lead_in_,
      res_,
      beast::bind_front_handler(&session::post_on_read_server, shared_from_this()));
}

void session::post_on_read_server(beast::error_code ec, std::size_t bytes_transferred) {
  check_error(ec, bytes_transferred, "post on read server");
  // log: ID: Received "RESPONSE" from SERVER
  lw_.log_response_from_server(res_, host);
  http::async_write(
      client_,
      res_,
      beast::bind_front_handler(&session::post_on_write_client, shared_from_this()));
}
void session::post_on_write_client(beast::error_code ec, std::size_t bytes_transferred) {
  check_error(ec, bytes_transferred, "get on write client");
  // log: ID: Responding "RESPONSE"
  lw_.log_response_to_client(res_);
  // log tunnel closed in do_close()
  lw_.log_tunnel_closed();
  do_close();
}

void session::on_connect_response(beast::error_code ec, std::size_t bytes_transferred) {
  check_error(ec, bytes_transferred, "on connect response");
  client_do_read();
  server_do_read();
}

void session::client_do_read() {
  client_.socket().async_read_some(
      boost::asio::buffer(client_buf_),
      beast::bind_front_handler(&session::client_on_read, shared_from_this()));
}
///to change
void session::client_on_read(beast::error_code ec, std::size_t bytes_transferred) {
  check_error(ec, bytes_transferred, "client on read");
  async_write(
      server_.socket(),
      boost::asio::buffer(client_buf_,
                          bytes_transferred),  //boost::asio::buffer(client_buf_, xfer),
      beast::bind_front_handler(&session::client_on_written, shared_from_this()));
}

void session::client_on_written(beast::error_code ec, std::size_t bytes_transferred) {
  check_error(ec, bytes_transferred, "client on written");
  client_do_read();
}

void session::server_do_read() {
  server_.socket().async_read_some(
      boost::asio::buffer(server_buf_),
      beast::bind_front_handler(&session::server_on_read, shared_from_this()));
}

void session::server_on_read(beast::error_code ec, std::size_t bytes_transferred) {
  check_error(ec, bytes_transferred, "server on read");
  async_write(client_.socket(),
              boost::asio::buffer(server_buf_, bytes_transferred),
              beast::bind_front_handler(&session::server_on_written, shared_from_this()));
}

void session::server_on_written(beast::error_code ec, std::size_t bytes_transferred) {
  check_error(ec, bytes_transferred, "server on written");
  server_do_read();
}

void session::do_close() {
  // Send a TCP shutdown
  beast::error_code ec;
  // log: ID: Tunnel closed
  client_.socket().shutdown(tcp::socket::shutdown_send, ec);
  server_.socket().shutdown(tcp::socket::shutdown_send, ec);
  // At this point the connection is closed gracefully
}

void session::check_error(beast::error_code ec,
                          std::size_t bytes_transferred,
                          char const * what) {
  boost::ignore_unused(bytes_transferred);
  if (ec) {
    return fail(ec, what);
  }
}

void session::send_bad_response(http::status status, std::string body) {
  res_ = {status, req_.version()};
  res_.set(beast::http::field::server, "My Server");
  res_.set(beast::http::field::content_type, "text/plain");
  res_.body() = body;
  res_.prepare_payload();
  // log error message
  lw_.log_error(body);
  http::async_write(
      client_,
      res_,
      beast::bind_front_handler(&session::on_write_bad_client, shared_from_this()));
}