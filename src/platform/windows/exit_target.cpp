#include "platform/windows/exit_target.hpp"
#include "core/vr_game_classifier.hpp"
#include "openvr.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <string_view>

namespace steamvr_gesture_exit::platform::windows {
namespace {
std::uint64_t token(const FILETIME& f) {
  ULARGE_INTEGER u{};
  u.LowPart = f.dwLowDateTime;
  u.HighPart = f.dwHighDateTime;
  return u.QuadPart;
}
enum class ProcessInspectionStatus {
  Available,
  Unavailable,
  AccessDenied,
  IdentityUnavailable,
  PathUnavailable
};
struct ProcessInspection {
  ProcessInspectionStatus status = ProcessInspectionStatus::Unavailable;
  std::wstring path;
  std::uint64_t creation_token = 0;
};
ProcessInspection inspect(std::uint32_t pid) {
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
  if (!h)
    return {GetLastError() == ERROR_ACCESS_DENIED ? ProcessInspectionStatus::AccessDenied
                                                  : ProcessInspectionStatus::Unavailable,
            {},
            0};
  FILETIME c{}, e{}, k{}, u{};
  if (!GetProcessTimes(h, &c, &e, &k, &u)) {
    const DWORD error = GetLastError();
    CloseHandle(h);
    return {error == ERROR_ACCESS_DENIED ? ProcessInspectionStatus::AccessDenied
                                         : ProcessInspectionStatus::IdentityUnavailable,
            {},
            0};
  }
  std::array<wchar_t, 32768> p{};
  DWORD n = static_cast<DWORD>(p.size());
  if (!QueryFullProcessImageNameW(h, 0, p.data(), &n) || !n) {
    const DWORD error = GetLastError();
    CloseHandle(h);
    return {error == ERROR_ACCESS_DENIED ? ProcessInspectionStatus::AccessDenied
                                         : ProcessInspectionStatus::PathUnavailable,
            {},
            0};
  }
  CloseHandle(h);
  return {ProcessInspectionStatus::Available, std::wstring(p.data(), n), token(c)};
}
std::wstring lower(std::wstring v) {
  std::transform(v.begin(), v.end(), v.begin(),
                 [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return v;
}
std::wstring utf8(const char* s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
  if (n <= 1)
    return {};
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
  w.pop_back();
  return w;
}
std::optional<std::string> property_string(vr::IVRApplications* applications,
                                           const std::string& key,
                                           vr::EVRApplicationProperty property,
                                           bool allow_unset = false) {
  std::array<char, 4096> value{};
  vr::EVRApplicationError error{};
  applications->GetApplicationPropertyString(key.c_str(), property, value.data(),
                                             static_cast<uint32_t>(value.size()), &error);
  if (allow_unset && error == vr::VRApplicationError_PropertyNotSet)
    return std::string{};
  if (error != vr::VRApplicationError_None || value[0] == '\0')
    return std::nullopt;
  return std::string(value.data());
}
std::optional<bool> property_bool(vr::IVRApplications* applications, const std::string& key,
                                  vr::EVRApplicationProperty property) {
  vr::EVRApplicationError error{};
  const bool value = applications->GetApplicationPropertyBool(key.c_str(), property, &error);
  // These exclusion flags are optional in application manifests. An omitted
  // flag has the same meaning as false; other API failures remain unavailable.
  if (error == vr::VRApplicationError_PropertyNotSet)
    return false;
  if (error != vr::VRApplicationError_None)
    return std::nullopt;
  return value;
}
}  // namespace
ExitTargetResolver::ExitTargetResolver(vr::IVRApplications* a) : applications_(a) {}
TargetResolution ExitTargetResolver::resolve_foreground() const {
  std::scoped_lock lock(applications_mutex_);
  TargetResolution result{};
  if (!applications_)
    return result;
  const auto scene_state = applications_->GetSceneApplicationState();
  result.scene_state = static_cast<int>(scene_state);
  if (scene_state != vr::EVRSceneApplicationState_Running &&
      scene_state != vr::EVRSceneApplicationState_Waiting) {
    result.game_status = CurrentVrGameStatus::SceneNotRunning;
    return result;
  }
  const auto pid = applications_->GetCurrentSceneProcessId();
  result.scene_pid = pid;
  if (!pid) {
    result.game_status = CurrentVrGameStatus::SceneProcessUnavailable;
    return result;
  }
  if (pid == GetCurrentProcessId()) {
    result.game_status = CurrentVrGameStatus::SelfProcessRejected;
    return result;
  }
  std::array<char, vr::k_unMaxApplicationKeyLength> key{};
  if (applications_->GetApplicationKeyByProcessId(
          pid, key.data(), static_cast<uint32_t>(key.size())) != vr::VRApplicationError_None ||
      key[0] == '\0') {
    result.game_status = CurrentVrGameStatus::ApplicationKeyUnavailable;
    return result;
  }
  const auto launch_type =
      property_string(applications_, key.data(), vr::VRApplicationProperty_LaunchType_String, true);
  const auto dashboard =
      property_bool(applications_, key.data(), vr::VRApplicationProperty_IsDashboardOverlay_Bool);
  const auto app_template =
      property_bool(applications_, key.data(), vr::VRApplicationProperty_IsTemplate_Bool);
  const auto internal =
      property_bool(applications_, key.data(), vr::VRApplicationProperty_IsInternal_Bool);
  const auto hidden =
      property_bool(applications_, key.data(), vr::VRApplicationProperty_IsHidden_Bool);
  if (!launch_type || !dashboard || !app_template || !internal || !hidden) {
    result.game_status = CurrentVrGameStatus::ApplicationMetadataUnavailable;
    return result;
  }
  // The process rendering the scene may be a registered subprocess rather
  // than GetApplicationProcessId(key). The reverse PID-to-key mapping above
  // is the identity evidence for this exact scene process.
  if (!core::is_current_vr_game_candidate(
          {key.data(), *launch_type, *dashboard, *app_template, *internal, *hidden})) {
    result.game_status = CurrentVrGameStatus::UnsafeApplication;
    return result;
  }
  const auto name =
      property_string(applications_, key.data(), vr::VRApplicationProperty_Name_String);
  CurrentVrGame game{pid, key.data(), name ? utf8(name->c_str()) : utf8(key.data())};
  if (game.display_name.empty())
    game.display_name = L"Unknown VR game";
  result.game_status = CurrentVrGameStatus::Available;
  result.game = game;
  const auto p = inspect(pid);
  switch (p.status) {
    case ProcessInspectionStatus::Unavailable:
      result.target_status = ExitTargetStatus::ProcessUnavailable;
      return result;
    case ProcessInspectionStatus::AccessDenied:
      result.target_status = ExitTargetStatus::ProcessAccessDenied;
      return result;
    case ProcessInspectionStatus::IdentityUnavailable:
      result.target_status = ExitTargetStatus::ProcessIdentityUnavailable;
      return result;
    case ProcessInspectionStatus::PathUnavailable:
      result.target_status = ExitTargetStatus::ProcessPathUnavailable;
      return result;
    case ProcessInspectionStatus::Available:
      break;
  }
  if (!process_is_safe(p.path)) {
    result.target_status = ExitTargetStatus::UnsafeProcess;
    return result;
  }
  result.target = app::RuntimeTarget{pid, p.creation_token, key.data(), p.path, game.display_name};
  result.target_status = ExitTargetStatus::Available;
  return result;
}
TargetValidation ExitTargetResolver::revalidate(const app::RuntimeTarget& t) const {
  if (!applications_ || !t.pid || t.app_key.empty())
    return TargetValidation::Mismatch;
  // Bound lock contention with the resolver thread. If it is still querying
  // SteamVR, fail closed and leave the locked game process untouched.
  std::unique_lock lock(applications_mutex_, std::defer_lock);
  if (!lock.try_lock_for(std::chrono::milliseconds(50)))
    return TargetValidation::Busy;
  auto p = inspect(t.pid);
  if (p.status != ProcessInspectionStatus::Available || p.creation_token != t.creation_token ||
      lower(p.path) != lower(t.executable_path))
    return TargetValidation::Mismatch;
  return app_key_matches_target(t.app_key, t.pid) ? TargetValidation::Matching
                                                  : TargetValidation::Mismatch;
}
bool ExitTargetResolver::auto_launch_enabled(const char* app_key) const {
  std::scoped_lock lock(applications_mutex_);
  return applications_ && applications_->IsApplicationInstalled(app_key) &&
         applications_->GetApplicationAutoLaunch(app_key);
}
std::pair<int, std::uint32_t> ExitTargetResolver::scene_identity() const {
  std::scoped_lock lock(applications_mutex_);
  if (!applications_)
    return {static_cast<int>(vr::EVRSceneApplicationState_None), 0};
  const auto state = applications_->GetSceneApplicationState();
  const bool active = state == vr::EVRSceneApplicationState_Running ||
                      state == vr::EVRSceneApplicationState_Waiting;
  return {static_cast<int>(state), active ? applications_->GetCurrentSceneProcessId() : 0};
}
bool ExitTargetResolver::app_key_matches_target(const std::string& raw,
                                                std::uint32_t expected_pid) const {
  // After confirmation the scene may return to SteamVR while the original
  // game is still closing. Revalidate the locked process, not the foreground.
  if (!applications_ || !expected_pid)
    return false;
  std::array<char, vr::k_unMaxApplicationKeyLength> mapped_key{};
  if (applications_->GetApplicationKeyByProcessId(expected_pid, mapped_key.data(),
                                                  static_cast<uint32_t>(mapped_key.size())) !=
          vr::VRApplicationError_None ||
      raw != mapped_key.data())
    return false;
  const auto launch_type =
      property_string(applications_, raw, vr::VRApplicationProperty_LaunchType_String, true);
  const auto dashboard =
      property_bool(applications_, raw, vr::VRApplicationProperty_IsDashboardOverlay_Bool);
  const auto app_template =
      property_bool(applications_, raw, vr::VRApplicationProperty_IsTemplate_Bool);
  const auto internal =
      property_bool(applications_, raw, vr::VRApplicationProperty_IsInternal_Bool);
  const auto hidden = property_bool(applications_, raw, vr::VRApplicationProperty_IsHidden_Bool);
  if (!launch_type || !dashboard || !app_template || !internal || !hidden)
    return false;
  return core::is_current_vr_game_candidate(
      {raw, *launch_type, *dashboard, *app_template, *internal, *hidden});
}
bool ExitTargetResolver::process_is_safe(const std::wstring& path) const {
  auto n = lower(std::filesystem::path(path).filename().wstring());
  constexpr std::array<std::wstring_view, 12> deny = {
      L"vrserver.exe",       L"vrmonitor.exe",  L"vrcompositor.exe", L"vrdashboard.exe",
      L"vrwebhelper.exe",    L"vrstartup.exe",  L"steam.exe",        L"steamservice.exe",
      L"steamwebhelper.exe", L"steamtours.exe", L"roomsetup.exe",    L"steamvr_gesture_exit.exe"};
  return std::find(deny.begin(), deny.end(), n) == deny.end();
}

const char* diagnostic_code(const CurrentVrGameStatus status) noexcept {
  switch (status) {
    case CurrentVrGameStatus::Available:
      return "AVAILABLE";
    case CurrentVrGameStatus::ApplicationsUnavailable:
      return "OPENVR_APPLICATIONS_UNAVAILABLE";
    case CurrentVrGameStatus::Resolving:
      return "RESOLVING_CURRENT_GAME";
    case CurrentVrGameStatus::SceneNotRunning:
      return "NO_SCENE_APPLICATION";
    case CurrentVrGameStatus::SceneProcessUnavailable:
      return "SCENE_PROCESS_UNAVAILABLE";
    case CurrentVrGameStatus::SelfProcessRejected:
      return "SELF_PROCESS_REJECTED";
    case CurrentVrGameStatus::ApplicationKeyUnavailable:
      return "APPLICATION_KEY_UNAVAILABLE";
    case CurrentVrGameStatus::ApplicationMetadataUnavailable:
      return "APPLICATION_METADATA_UNAVAILABLE";
    case CurrentVrGameStatus::UnsafeApplication:
      return "UNSAFE_SCENE_APPLICATION";
  }
  return "UNKNOWN";
}
const char* diagnostic_code(const ExitTargetStatus status) noexcept {
  switch (status) {
    case ExitTargetStatus::Available:
      return "AVAILABLE";
    case ExitTargetStatus::CurrentGameUnavailable:
      return "CURRENT_GAME_UNAVAILABLE";
    case ExitTargetStatus::ProcessUnavailable:
      return "PROCESS_UNAVAILABLE";
    case ExitTargetStatus::ProcessAccessDenied:
      return "PROCESS_ACCESS_DENIED";
    case ExitTargetStatus::ProcessIdentityUnavailable:
      return "PROCESS_IDENTITY_UNAVAILABLE";
    case ExitTargetStatus::ProcessPathUnavailable:
      return "PROCESS_PATH_UNAVAILABLE";
    case ExitTargetStatus::UnsafeProcess:
      return "UNSAFE_PROCESS";
  }
  return "UNKNOWN";
}
}  // namespace steamvr_gesture_exit::platform::windows
