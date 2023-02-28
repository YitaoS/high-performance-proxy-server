#ifndef PROXY_SERVER
#define PROXY_SERVER
#include "session.hpp"

namespace beast = boost::beast;  // from <boost/beast.hpp>
namespace http = beast::http;    // from <boost/beast/http.hpp>
namespace net = boost::asio;     // from <boost/asio.hpp>
namespace ph = boost::asio::placeholders;
using tcp = boost::asio::ip::tcp;  // from <boost/asio/ip/tcp.hpp>

class listener : public std::enable_shared_from_this<listener> {
  net::io_context & ioc_;
  tcp::acceptor acceptor_;
  std::ofstream & logfile;
  Cache<std::string, CachedResponse> http_cache;
  int num_of_session;
  std::mutex my_mutex;

  void fail(beast::error_code ec, char const * what) {
    std::cerr << what << ": " << ec.message() << "\n";
  }

 public:
  listener(net::io_context & ioc,
           tcp::endpoint endpoint,
           std::ofstream & logfile,
           int capacity);

  // Start accepting incoming connections
  void run() { do_accept(); }

 private:
  void do_accept();

  void on_accept(beast::error_code ec, tcp::socket socket);
};
#endif  //PROXY_SERVER