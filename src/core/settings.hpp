#pragma once

#include "core/types.hpp"

#include <filesystem>
#include <optional>
#include <string>

namespace bailout::core {

struct Settings {
  static constexpr int current_version = 1;

  int version{current_version};
  Hand configured_hand{Hand::Left};
  int countdown_seconds{5};
  int control_timeout_seconds{8};
  bool countdown_sound{true};
  Language language{Language::Auto};

  [[nodiscard]] static Settings defaults() noexcept;
  [[nodiscard]] std::optional<std::string> validation_error() const;
  friend bool operator==(const Settings&, const Settings&) = default;
};

enum class SettingsLoadStatus {
  Loaded,
  DefaultsMissing,
  RecoveredCorrupt,
  RecoveredIncompatible,
};

struct SettingsLoadResult {
  Settings settings;
  SettingsLoadStatus status{SettingsLoadStatus::Loaded};
  std::string diagnostic_code;
};

class SettingsStore {
 public:
  explicit SettingsStore(std::filesystem::path path);

  [[nodiscard]] SettingsLoadResult load() const;
  [[nodiscard]] bool save(const Settings& settings, std::string* error = nullptr) const;
  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

 private:
  std::filesystem::path path_;
};

[[nodiscard]] std::string to_string(Hand value);
[[nodiscard]] std::string to_string(Language value);

}  // namespace bailout::core
