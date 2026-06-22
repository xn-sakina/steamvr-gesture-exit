#pragma once

#include "flow.h"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace sbo {

struct DiagnosticSummary {
  std::string currentGame = "unavailable";
  std::string exitTarget = "unavailable";
  std::string controllerSetup = "unknown";
  std::string settingsStatus = "not_loaded";
  LastExitStatus lastExit = LastExitStatus::none;
};

class DiagnosticLog {
 public:
  explicit DiagnosticLog(std::filesystem::path path);
  void write(std::string eventName, std::string code = {}, const ExitTargetIdentity* target = nullptr);
  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
  std::mutex mutex_;
  std::ofstream stream_;
};

std::string sanitize_log_field(std::string value);
std::string timestamp_utc();

}  // namespace sbo
