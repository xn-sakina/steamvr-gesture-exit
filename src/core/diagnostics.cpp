#include "core/diagnostics.hpp"
#include "json/json.h"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace bailout::core {
namespace {

constexpr std::uintmax_t kMaxLogBytes = 4 * 1024 * 1024;

std::string now_utc() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t value = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &value);
#else
  gmtime_r(&value, &utc);
#endif
  std::ostringstream output;
  output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return output.str();
}

}  // namespace

std::string_view to_string(const DiagnosticEventType value) noexcept {
  switch (value) {
    case DiagnosticEventType::Lifecycle:
      return "lifecycle";
    case DiagnosticEventType::AvailabilityChanged:
      return "availability_changed";
    case DiagnosticEventType::GestureRecognized:
      return "gesture_recognized";
    case DiagnosticEventType::StateTransition:
      return "state_transition";
    case DiagnosticEventType::SettingsChanged:
      return "settings_changed";
    case DiagnosticEventType::SettingsRecovered:
      return "settings_recovered";
    case DiagnosticEventType::ExitResult:
      return "exit_result";
    case DiagnosticEventType::Error:
      return "error";
  }
  return "error";
}

std::string serialize_diagnostic_record(const DiagnosticRecord& record,
                                        const std::string_view timestamp_utc) {
  Json::Value root(Json::objectValue);
  root["timestamp"] = std::string(timestamp_utc);
  root["event"] = std::string(to_string(record.event));
  root["level"] = record.debug_only ? "debug"
                  : record.event == DiagnosticEventType::Error ? "error" : "info";
  const auto field = [&](std::string_view name, std::string_view value) {
    if (!value.empty())
      root[std::string(name)] = std::string(value);
  };
  field("code", record.code);
  field("from", record.from_state);
  field("to", record.to_state);
  field("status", record.status);
  field("current_game_status", record.current_game_status);
  field("exit_target_status", record.exit_target_status);
  field("setting", record.setting_name);
  field("value", record.setting_value);
  field("target", record.target_name);
  if (record.available)
    root["available"] = *record.available;
  if (!record.details.empty()) {
    Json::Value details(Json::objectValue);
    for (const auto& [name, value] : record.details)
      details[name] = value;
    root["details"] = std::move(details);
  }
  Json::FastWriter writer;
  writer.omitEndingLineFeed();
  return writer.write(root);
}

DiagnosticLog::DiagnosticLog(std::filesystem::path path) : path_(std::move(path)) {}
DiagnosticLog::~DiagnosticLog() { stop_async(); }

void DiagnosticLog::start_async() {
  std::scoped_lock lock(queue_mutex_);
  if (async_enabled_)
    return;
  stopping_ = false;
  worker_ = std::thread([this] { run_async(); });
  async_enabled_ = true;
}

void DiagnosticLog::stop_async() {
  {
    std::scoped_lock lock(queue_mutex_);
    if (!async_enabled_)
      return;
    async_enabled_ = false;
    stopping_ = true;
  }
  queue_wake_.notify_one();
  if (worker_.joinable())
    worker_.join();
}

void DiagnosticLog::run_async() {
  for (;;) {
    std::unique_lock lock(queue_mutex_);
    queue_wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
    if (queue_.empty())
      return;
    auto record = std::move(queue_.front());
    queue_.pop_front();
    lock.unlock();
    (void)append_sync(record, nullptr);
  }
}

bool DiagnosticLog::append(const DiagnosticRecord& record, std::string* error) {
#ifdef NDEBUG
  if (record.debug_only)
    return true;
#endif
  if (!error) {
    std::scoped_lock lock(queue_mutex_);
    if (async_enabled_) {
      queue_.push_back(record);
      queue_wake_.notify_one();
      return true;
    }
  }
  return append_sync(record, error);
}

bool DiagnosticLog::append_sync(const DiagnosticRecord& record, std::string* error) {
  std::scoped_lock lock(mutex_);
  std::error_code ec;
  if (!path_.parent_path().empty())
    std::filesystem::create_directories(path_.parent_path(), ec);
  if (ec) {
    if (error)
      *error = "LOG_CREATE_DIRECTORY_FAILED";
    return false;
  }
  const std::string line = serialize_diagnostic_record(record, now_utc()) + '\n';
  if (line.size() > kMaxLogBytes) {
    if (error)
      *error = "LOG_RECORD_TOO_LARGE";
    return false;
  }
  const bool exists = std::filesystem::exists(path_, ec);
  if (ec) {
    if (error)
      *error = "LOG_STAT_FAILED";
    return false;
  }
  if (exists) {
    const auto size = std::filesystem::file_size(path_, ec);
    if (ec) {
      if (error)
        *error = "LOG_STAT_FAILED";
      return false;
    }
    if (size > kMaxLogBytes || line.size() > kMaxLogBytes - size) {
      if (size > kMaxLogBytes) {
        // An unbounded log from an older version must not become an unbounded backup.
        std::filesystem::remove(path_, ec);
      } else {
        auto backup = path_;
        backup += ".1";
        std::filesystem::remove(backup, ec);
        if (!ec)
          std::filesystem::rename(path_, backup, ec);
      }
      if (ec) {
        if (error)
          *error = "LOG_ROTATE_FAILED";
        return false;
      }
    }
  }
  std::ofstream output(path_, std::ios::binary | std::ios::app);
  output << line;
  output.flush();
  const bool written = static_cast<bool>(output);
  output.close();
  if (!written || output.fail()) {
    if (error)
      *error = "LOG_WRITE_FAILED";
    return false;
  }
  return true;
}

}  // namespace bailout::core
