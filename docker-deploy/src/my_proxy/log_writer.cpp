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

void LogWriter::log_response_from_server(const http::response<http::string_body> & response,
                              std::string server_name) {
  std::lock_guard<std::mutex> lock(log_mutex);
  logfile << id << ": Receiving \""
          << "HTTP/" << response.version() << " " << response.result_int() << " "
          << response.reason() << "\" from " << server_name << std::endl;
}

void LogWriter::log_response_to_client(const http::response<http::string_body> & response) {
    std::lock_guard<std::mutex> lock(log_mutex);
    logfile << id << ": Responding \""
            << "HTTP/" << response.version() << " " << response.result_int() << " "
            << response.reason() << "\"" << std::endl;
}

void LogWriter::log_tunnel_closed() {
  std::lock_guard<std::mutex> lock(log_mutex);
  logfile << id << ": Tunnel closed" << std::endl;
}

void LogWriter::log_note(std::string note) {
  std::lock_guard<std::mutex> lock(log_mutex);
  logfile << id << ": NOTE " << note << std::endl;
}

void LogWriter::log_warning(std::string warning) {
  std::lock_guard<std::mutex> lock(log_mutex);
  logfile << id << ": WARNING " << warning << std::endl;
}

void LogWriter::log_error(std::string error) {
  std::lock_guard<std::mutex> lock(log_mutex);
  logfile << id << ": ERROR " << error << std::endl;
}

void LogWriter::log_not_in_cache() {
  std::lock_guard<std::mutex> lock(log_mutex);
  logfile << id << ": not in cache" << std::endl;
}

void LogWriter::log_expired(std::chrono::steady_clock::time_point time) {
  std::lock_guard<std::mutex> lock(log_mutex);
  logfile << id << ": in cache, but expired at " << to_utc_string(time) << std::endl;
}

void LogWriter::log_require_validation() {
  std::lock_guard<std::mutex> lock(log_mutex);
  logfile << id << ": in cache, requires validation" << std::endl;
}

void LogWriter::log_valid() {
  std::lock_guard<std::mutex> lock(log_mutex);
  logfile << id << ": in cache, valid" << std::endl;
}

void LogWriter::log_not_cacheable(std::string reason) {
  std::lock_guard<std::mutex> lock(log_mutex);
  logfile << id << ": not cacheable because " << reason << std::endl;
}

void LogWriter::log_cached_with_expire_time(std::chrono::steady_clock::time_point time) {
  std::lock_guard<std::mutex> lock(log_mutex);
  logfile << id << ": cached, expires at " << to_utc_string(time) << std::endl;
}

void LogWriter::log_cached_with_revalidation() {
  std::lock_guard<std::mutex> lock(log_mutex);
  logfile << id << ": cached, but requires re-validation" << std::endl;
}