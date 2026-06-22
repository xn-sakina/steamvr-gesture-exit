#pragma once

#include "diagnostics.h"
#include "flow.h"

#include <filesystem>
#include <optional>
#include <string>

namespace sbo {

struct SettingsLoadResult {
  Settings settings;
  bool recovered = false;
  std::string status = "loaded";
};

enum class ProcessCloseResult {
  gracefulRequested,
  targetGone,
  identityMismatch,
  forceClosed,
  failed,
};

std::filesystem::path default_data_dir();
std::filesystem::path default_settings_path();
std::filesystem::path default_log_path();

class SettingsStore {
 public:
  explicit SettingsStore(std::filesystem::path path = default_settings_path());
  SettingsLoadResult load();
  bool save(const Settings& settings);
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

class LanguageService {
 public:
  static std::string resolve(LanguageMode mode, const std::string& steamLanguage = {});
};

class ProcessController {
 public:
  std::optional<ExitTargetIdentity> identity_from_pid(std::uint32_t pid, std::string appKey, std::string displayName) const;
  bool is_alive(const ExitTargetIdentity& target) const;
  bool same_identity_alive(const ExitTargetIdentity& target) const;
  ProcessCloseResult request_graceful_close(const ExitTargetIdentity& target) const;
  ProcessCloseResult force_close(const ExitTargetIdentity& target) const;
};

std::string settings_to_json(const Settings& settings);
SettingsLoadResult parse_settings_json(const std::string& text);

}  // namespace sbo
