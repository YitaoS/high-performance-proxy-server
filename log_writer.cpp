#include "log_writer.hpp"

std::string LogWriter::current_utc_time() {
  std::time_t now = std::time(nullptr);
  char buf[128];
  std::strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Y", std::gmtime(&now));
  return buf;
}

std::string LogWriter::to_utc_string(const std::chrono::steady_clock::time_point & tp) {
  // Convert steady_clock::time_point to system_clock::time_point
  auto sys_tp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      tp - std::chrono::steady_clock::now() + std::chrono::system_clock::now());

  // Convert system_clock::time_point to time_t
  auto time = std::chrono::system_clock::to_time_t(sys_tp);

  // Convert time_t to struct tm
  std::tm * tm_utc = std::gmtime(&time);

  // Format struct tm as ISO 8601 string
  char buf[30];
  std::strftime(buf, sizeof(buf), "%a %b %d %H:%M:%S %Y", tm_utc);

  return std::string(buf, std::strlen(buf));
}