#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace bailout::core {

enum class DiagnosticEventType {
  Lifecycle,
  AvailabilityChanged,
  GestureRecognized,
  StateTransition,
  SettingsChanged,
  SettingsRecovered,
  ExitResult,
  Error,
};

// Fixed fields keep diagnostics useful without providing an accidental channel
// for pose streams, game content, chat, audio, or process enumeration.
struct DiagnosticRecord {
  DiagnosticRecord() = default;
  DiagnosticRecord(DiagnosticEventType kind, std::string event_code)
      : event(kind), code(std::move(event_code)) {}
  DiagnosticEventType event{DiagnosticEventType::Lifecycle};
  std::string code;
  std::string from_state;
  std::string to_state;
  std::string status;
  std::string current_game_status;
  std::string exit_target_status;
  std::string setting_name;
  std::string setting_value;
  std::string target_name;
  std::optional<bool> available;
  bool debug_only = false;
  // Code-owned keys and bounded, non-sensitive measurements only.
  std::map<std::string, std::string> details;
};

class DiagnosticLog {
 public:
  explicit DiagnosticLog(std::filesystem::path path);

  [[nodiscard]] bool append(const DiagnosticRecord& record, std::string* error = nullptr);
  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }

 private:
  std::filesystem::path path_;
  std::mutex mutex_;
};

[[nodiscard]] std::string_view to_string(DiagnosticEventType value) noexcept;
[[nodiscard]] std::string serialize_diagnostic_record(const DiagnosticRecord& record,
                                                      std::string_view timestamp_utc);

}  // namespace bailout::core
