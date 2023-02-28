#include "proxy_server.hpp"

listener::listener(net::io_context & ioc,
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

void listener::do_accept() {
    // The new connection gets its own strand
    acceptor_.async_accept(
        net::make_strand(ioc_),
        beast::bind_front_handler(&listener::on_accept, shared_from_this()));
}

void listener::on_accept(beast::error_code ec, tcp::socket socket) {
    if (ec) {
      fail(ec, "accept");
      return;  // To avoid infinite loop
    }
    else {
      // Create the session and run it
      std::make_shared<session>(
          std::move(socket), num_of_session++, logfile, http_cache, my_mutex)
          ->run();
    }
    do_accept();

    // Accept another connection
}

int main(int argc, char * argv[]) {
  // Check command line arguments.
  if (argc != 4) {
    std::cerr << "Usage: http-server-async <address> <port> <threads>\n"
              << "Example:\n"
              << "    http-server-async 0.0.0.0 8080 1\n";
    return EXIT_FAILURE;
  }

  auto const address = net::ip::make_address(argv[1]);
  auto const port = static_cast<unsigned short>(std::atoi(argv[2]));
  auto const threads = std::max<int>(1, std::atoi(argv[3]));

  // The io_context is required for all I/O
  net::io_context ioc{threads};

  //create a stream for log writing
  // Create and launch a listening port
  std::ofstream log_file("log.txt", std::ios_base::app);
  std::make_shared<listener>(ioc, tcp::endpoint{address, port}, log_file, 50)->run();

  // Run the I/O service on the requested number of threads
  std::vector<std::thread> v;
  v.reserve(threads - 1);
  for (auto i = threads - 1; i > 0; --i)
    v.emplace_back([&ioc] { ioc.run(); });
  ioc.run();

  return EXIT_SUCCESS;
}