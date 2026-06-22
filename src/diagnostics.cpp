#include "diagnostics.h"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace sbo {

DiagnosticLog::DiagnosticLog(std::filesystem::path path) : path_(std::move(path)) {
  std::error_code ec;
  std::filesystem::create_directories(path_.parent_path(), ec);
  stream_.open(path_, std::ios::app);
}

void DiagnosticLog::write(std::string eventName, std::string code, const ExitTargetIdentity* target) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stream_) return;

  stream_ << "ts=" << timestamp_utc() << " event=" << sanitize_log_field(std::move(eventName));
  if (!code.empty()) stream_ << " code=" << sanitize_log_field(std::move(code));
  if (target != nullptr) {
    stream_ << " pid=" << target->pid;
    if (!target->appKey.empty()) stream_ << " app_key=" << sanitize_log_field(target->appKey);
    if (!target->displayName.empty()) stream_ << " display=" << sanitize_log_field(target->displayName);
  }
  stream_ << '\n';
  stream_.flush();
}

std::string sanitize_log_field(std::string value) {
  for (char& ch : value) {
    if (ch == '\n' || ch == '\r' || ch == '\t' || ch == ' ') ch = '_';
  }
  if (value.size() > 120) value.resize(120);
  return value;
}

std::string timestamp_utc() {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &time);
#else
  gmtime_r(&time, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

}  // namespace sbo
