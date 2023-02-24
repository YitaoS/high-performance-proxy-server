#include "log_writer.hpp"

std::string LogWriter::current_utc_time() {
  std::time_t now = std::time(nullptr);
  char buf[128];
  std::strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Y", std::gmtime(&now));
  return buf;
}