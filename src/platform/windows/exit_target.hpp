#pragma once
#include "app/runtime_interfaces.h"
#include <optional>
namespace vr { class IVRApplications; }
namespace bailout::platform::windows {
class ExitTargetResolver {
 public:
  explicit ExitTargetResolver(vr::IVRApplications* applications);
  std::optional<app::RuntimeTarget> resolve_foreground() const;
  bool revalidate(const app::RuntimeTarget& target) const;
 private:
  bool app_is_safe_scene(const std::string& app_key, std::uint32_t expected_pid) const;
  bool process_is_safe(const std::wstring& executable_path) const;
  vr::IVRApplications* applications_ = nullptr;
};
}
