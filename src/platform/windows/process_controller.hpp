#pragma once
#include "app/runtime_interfaces.h"
#include <chrono>
#include <optional>
#include <string_view>
namespace steamvr_gesture_exit::platform::windows {
enum class CloseStatus {
  Idle,
  GracefulRequested,
  ExitedGracefully,
  ForceRequired,
  ForcePending,
  ForceTimedOut,
  ForceSucceeded,
  Failed,
  IdentityInvalid
};
class ProcessController {
 public:
  static constexpr auto kGracePeriod = std::chrono::seconds(5);
  static constexpr auto kForceWait = std::chrono::seconds(5);
  ~ProcessController() {
    clear();
  }
  bool request_graceful_close(const app::RuntimeTarget&, std::chrono::steady_clock::time_point);
  CloseStatus poll(std::chrono::steady_clock::time_point);
  CloseStatus force_close(std::chrono::steady_clock::time_point);
  void clear();
  CloseStatus status() const {
    return status_;
  }
  std::string_view error_code() const {
    return error_code_;
  }

 private:
  bool identity_matches() const;
  std::optional<app::RuntimeTarget> target_;
  void* process_ = nullptr;
  std::chrono::steady_clock::time_point deadline_{};
  std::chrono::steady_clock::time_point force_deadline_{};
  CloseStatus status_ = CloseStatus::Idle;
  std::string_view error_code_{};
};
}  // namespace steamvr_gesture_exit::platform::windows
