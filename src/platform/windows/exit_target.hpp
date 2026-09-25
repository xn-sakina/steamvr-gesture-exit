#pragma once
#include "app/runtime_interfaces.h"
#include <mutex>
#include <optional>
#include <utility>
namespace vr {
class IVRApplications;
}
namespace bailout::platform::windows {

enum class CurrentVrGameStatus {
  Available,
  ApplicationsUnavailable,
  Resolving,
  SceneNotRunning,
  SceneProcessUnavailable,
  SelfProcessRejected,
  ApplicationKeyUnavailable,
  ApplicationMetadataUnavailable,
  UnsafeApplication,
};

enum class ExitTargetStatus {
  Available,
  CurrentGameUnavailable,
  ProcessUnavailable,
  ProcessAccessDenied,
  ProcessIdentityUnavailable,
  ProcessPathUnavailable,
  UnsafeProcess,
};

struct CurrentVrGame {
  std::uint32_t pid = 0;
  std::string app_key;
  std::wstring display_name;
};

struct TargetResolution {
  std::uint32_t scene_pid = 0;
  int scene_state = 0;
  CurrentVrGameStatus game_status = CurrentVrGameStatus::ApplicationsUnavailable;
  ExitTargetStatus target_status = ExitTargetStatus::CurrentGameUnavailable;
  std::optional<CurrentVrGame> game;
  std::optional<app::RuntimeTarget> target;
};

enum class TargetValidation { Matching, Mismatch, Busy };

[[nodiscard]] const char* diagnostic_code(CurrentVrGameStatus status) noexcept;
[[nodiscard]] const char* diagnostic_code(ExitTargetStatus status) noexcept;

class ExitTargetResolver {
 public:
  explicit ExitTargetResolver(vr::IVRApplications* applications);
  TargetResolution resolve_foreground() const;
  TargetValidation revalidate(const app::RuntimeTarget& target) const;
  bool auto_launch_enabled(const char* app_key) const;
  std::pair<int, std::uint32_t> scene_identity() const;

 private:
  bool app_key_matches_target(const std::string& app_key, std::uint32_t expected_pid) const;
  bool process_is_safe(const std::wstring& executable_path) const;
  vr::IVRApplications* applications_ = nullptr;
  mutable std::timed_mutex applications_mutex_;
};
}  // namespace bailout::platform::windows
