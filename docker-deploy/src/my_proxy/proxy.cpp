#include "proxy_server.hpp"

std::mutex global_mutex;

int main(int argc, char * argv[]) {
  // Call the daemon system call
  if (daemon(0, 0) < 0) {
    std::cerr << "Failed to create daemon process\n";
    std::exit(EXIT_FAILURE);
  }
  // Execute the main process
  while (true) {
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
    std::make_shared<listener>(
        ioc, tcp::endpoint{address, port}, log_file, 50, global_mutex)
        ->run();

    // Run the I/O service on the requested number of threads
    std::vector<std::thread> v;
    v.reserve(threads - 1);
    for (auto i = threads - 1; i > 0; --i)
      v.emplace_back([&ioc] { ioc.run(); });
    ioc.run();
  }
  return EXIT_SUCCESS;
}