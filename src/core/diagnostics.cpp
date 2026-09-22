#include "core/diagnostics.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace bailout::core {
namespace {

std::string escape_json(std::string_view value) {
  std::string output;
  output.reserve(value.size());
  for (const unsigned char character : value) {
    switch (character) {
      case '\"': output += "\\\""; break;
      case '\\': output += "\\\\"; break;
      case '\n': output += "\\n"; break;
      case '\r': output += "\\r"; break;
      case '\t': output += "\\t"; break;
      default:
        if (character < 0x20) {
          std::ostringstream encoded;
          encoded << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                  << static_cast<int>(character);
          output += encoded.str();
        } else {
          output += static_cast<char>(character);
        }
    }
  }
  return output;
}

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
    case DiagnosticEventType::Lifecycle: return "lifecycle";
    case DiagnosticEventType::AvailabilityChanged: return "availability_changed";
    case DiagnosticEventType::GestureRecognized: return "gesture_recognized";
    case DiagnosticEventType::StateTransition: return "state_transition";
    case DiagnosticEventType::SettingsChanged: return "settings_changed";
    case DiagnosticEventType::SettingsRecovered: return "settings_recovered";
    case DiagnosticEventType::ExitResult: return "exit_result";
    case DiagnosticEventType::Error: return "error";
  }
  return "error";
}

std::string serialize_diagnostic_record(const DiagnosticRecord& record,
                                        const std::string_view timestamp_utc) {
  std::ostringstream output;
  output << "{\"timestamp\":\"" << escape_json(timestamp_utc) << "\",\"event\":\""
         << to_string(record.event) << '"';
  const auto field = [&](std::string_view name, std::string_view value) {
    if (!value.empty()) output << ",\"" << name << "\":\"" << escape_json(value) << '"';
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
  if (record.available) output << ",\"available\":" << (*record.available ? "true" : "false");
  output << '}';
  return output.str();
}

DiagnosticLog::DiagnosticLog(std::filesystem::path path) : path_(std::move(path)) {}

bool DiagnosticLog::append(const DiagnosticRecord& record, std::string* error) {
  std::scoped_lock lock(mutex_);
  std::error_code ec;
  if (!path_.parent_path().empty()) std::filesystem::create_directories(path_.parent_path(), ec);
  if (ec) {
    if (error) *error = "LOG_CREATE_DIRECTORY_FAILED";
    return false;
  }
  std::ofstream output(path_, std::ios::binary | std::ios::app);
  output << serialize_diagnostic_record(record, now_utc()) << '\n';
  if (!output) {
    if (error) *error = "LOG_WRITE_FAILED";
    return false;
  }
  return true;
}

}  // namespace bailout::core
