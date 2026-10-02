#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>

namespace steamvr_gesture_exit::core {

// Persists only completed exits initiated by this application. Cancels,
// failures and games closed outside the application never replace the record.
class LastExitStore {
 public:
  explicit LastExitStore(std::filesystem::path path);

  [[nodiscard]] std::optional<std::chrono::system_clock::time_point> load(
      std::string* error = nullptr) const;
  [[nodiscard]] bool save(std::chrono::system_clock::time_point time,
                          std::string* error = nullptr) const;
  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

}  // namespace steamvr_gesture_exit::core
