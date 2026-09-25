#pragma once

#include "core/settings.hpp"

#include <cstddef>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bailout::core {

struct SettingsChange {
  Settings settings;
  std::string name;
  std::string value;
  std::chrono::steady_clock::time_point requested_at{};
};

struct SettingsSaveResult {
  SettingsChange change;
  bool saved = false;
  std::string error;
  std::chrono::steady_clock::time_point started_at{};
  std::chrono::steady_clock::time_point completed_at{};
};

// Writes snapshots in click order. The caller applies only successful results.
class SettingsPersistenceWorker {
 public:
  explicit SettingsPersistenceWorker(std::filesystem::path path);
  ~SettingsPersistenceWorker();
  SettingsPersistenceWorker(const SettingsPersistenceWorker&) = delete;
  SettingsPersistenceWorker& operator=(const SettingsPersistenceWorker&) = delete;

  [[nodiscard]] bool enqueue(SettingsChange change);
  [[nodiscard]] std::vector<SettingsSaveResult> take_results();
  void stop();

 private:
  void run();
  static constexpr size_t kMaxOutstanding = 64;
  SettingsStore store_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<SettingsChange> pending_;
  std::deque<SettingsSaveResult> completed_;
  size_t outstanding_ = 0;
  bool stop_ = false;
  std::thread thread_;
};

}  // namespace bailout::core
