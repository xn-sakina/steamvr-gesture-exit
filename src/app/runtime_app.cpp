#include "app/runtime_app.hpp"
#include "platform/windows/paths.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <thread>

namespace bailout::app {
namespace {
constexpr char kAppKey[] = "dev.kanamio.steamvrbailout";
constexpr auto kMaxTargetResolutionAge = std::chrono::seconds(2);
const char* wrist_action_name(platform::openvr::OverlayAction action) {
  switch (action) {
    case platform::openvr::OverlayAction::Primary:
      return "primary";
    case platform::openvr::OverlayAction::Confirm:
      return "confirm";
    case platform::openvr::OverlayAction::Dismiss:
      return "dismiss";
    case platform::openvr::OverlayAction::Cancel:
      return "cancel";
  }
  return "unknown";
}
const char* tracking_result_name(int value) {
  switch (static_cast<vr::ETrackingResult>(value)) {
    case vr::TrackingResult_Uninitialized:
      return "uninitialized";
    case vr::TrackingResult_Calibrating_InProgress:
      return "calibrating";
    case vr::TrackingResult_Calibrating_OutOfRange:
      return "calibrating_out_of_range";
    case vr::TrackingResult_Running_OK:
      return "running_ok";
    case vr::TrackingResult_Running_OutOfRange:
      return "running_out_of_range";
    case vr::TrackingResult_Fallback_RotationOnly:
      return "rotation_only";
  }
  return "unavailable";
}
class ScopedHandle {
 public:
  explicit ScopedHandle(HANDLE value = nullptr) : value_(value) {}
  ~ScopedHandle() {
    if (value_ && value_ != INVALID_HANDLE_VALUE)
      CloseHandle(value_);
  }
  ScopedHandle(const ScopedHandle&) = delete;
  ScopedHandle& operator=(const ScopedHandle&) = delete;
  HANDLE get() const {
    return value_;
  }

 private:
  HANDLE value_;
};

class TimerResolution {
 public:
  ~TimerResolution() {
    set_active(false);
  }
  void set_active(bool active) {
    if (active && !active_)
      active_ = timeBeginPeriod(1) == TIMERR_NOERROR;
    else if (!active && active_) {
      timeEndPeriod(1);
      active_ = false;
    }
  }

 private:
  bool active_ = false;
};

uint64_t process_creation_token(HANDLE process) {
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
    return 0;
  ULARGE_INTEGER token{};
  token.LowPart = created.dwLowDateTime;
  token.HighPart = created.dwHighDateTime;
  return token.QuadPart;
}

std::string narrow(const std::wstring& w) {
  if (w.empty())
    return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr,
                              nullptr);
  std::string s(static_cast<size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr,
                      nullptr);
  return s;
}
std::wstring widen(std::string_view s) {
  if (s.empty())
    return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::wstring local_timestamp(const std::chrono::system_clock::time_point time) {
  const std::time_t raw = std::chrono::system_clock::to_time_t(time);
  std::tm local{};
  if (localtime_s(&local, &raw) != 0)
    return {};
  std::array<wchar_t, 20> value{};
  if (std::wcsftime(value.data(), value.size(), L"%Y-%m-%d %H:%M:%S", &local) == 0)
    return {};
  return value.data();
}

class SteamVrServerMonitor {
 public:
  bool attach() {
    std::array<char, 4096> runtime_path{};
    uint32_t required = 0;
    if (!vr::VR_GetRuntimePath(runtime_path.data(), static_cast<uint32_t>(runtime_path.size()),
                               &required))
      return false;
    const auto expected =
        (std::filesystem::path(widen(runtime_path.data())) / L"bin" / L"win64" / L"vrserver.exe")
            .lexically_normal()
            .wstring();
    ScopedHandle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.get() == INVALID_HANDLE_VALUE)
      return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry))
      return false;
    uint64_t latest_creation = 0;
    do {
      if (_wcsicmp(entry.szExeFile, L"vrserver.exe") != 0)
        continue;
      HANDLE process =
          OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, entry.th32ProcessID);
      if (!process)
        continue;
      std::array<wchar_t, 32768> image{};
      DWORD length = static_cast<DWORD>(image.size());
      const bool matches =
          QueryFullProcessImageNameW(process, 0, image.data(), &length) &&
          _wcsicmp(
              std::filesystem::path(std::wstring(image.data(), length)).lexically_normal().c_str(),
              expected.c_str()) == 0;
      if (matches && WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
        const auto creation = process_creation_token(process);
        if (creation > latest_creation) {
          if (process_)
            CloseHandle(process_);
          process_ = process;
          latest_creation = creation;
          continue;
        }
      }
      CloseHandle(process);
    } while (Process32NextW(snapshot.get(), &entry));
    return process_ != nullptr;
  }
  ~SteamVrServerMonitor() {
    if (process_)
      CloseHandle(process_);
  }
  SteamVrServerMonitor(const SteamVrServerMonitor&) = delete;
  SteamVrServerMonitor& operator=(const SteamVrServerMonitor&) = delete;
  SteamVrServerMonitor() = default;
  bool alive() const {
    return process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
  }

 private:
  HANDLE process_ = nullptr;
};

// Process inspection and application metadata requests may wait on SteamVR or
// Windows. Keep them off the thread that polls overlay input and submits UI.
class ForegroundResolutionWorker {
 public:
  ForegroundResolutionWorker(const platform::windows::ExitTargetResolver& resolver,
                             core::DiagnosticLog& log)
      : thread_([this, &resolver, &log] {
          auto last_slow_log = std::chrono::steady_clock::time_point{};
          auto next_autolaunch_check = std::chrono::steady_clock::time_point{};
          while (!stop_) {
            const auto started = std::chrono::steady_clock::now();
            try {
              if (started >= next_autolaunch_check) {
                autolaunch_disabled_ = !resolver.auto_launch_enabled(kAppKey);
                next_autolaunch_check = started + std::chrono::seconds(1);
              }
              auto resolution = resolver.resolve_foreground();
              // A scene switch during process inspection invalidates the
              // result even if the old process is still running.
              const auto [verified_state, verified_pid] = resolver.scene_identity();
              const bool scene_active =
                  verified_state == static_cast<int>(vr::EVRSceneApplicationState_Running) ||
                  verified_state == static_cast<int>(vr::EVRSceneApplicationState_Waiting);
              if (resolution.scene_state != verified_state ||
                  resolution.scene_pid != verified_pid) {
                resolution = {};
                resolution.scene_state = verified_state;
                resolution.scene_pid = verified_pid;
                resolution.game_status =
                    verified_pid   ? platform::windows::CurrentVrGameStatus::Resolving
                    : scene_active ? platform::windows::CurrentVrGameStatus::SceneProcessUnavailable
                                   : platform::windows::CurrentVrGameStatus::SceneNotRunning;
              }
              const auto finished = std::chrono::steady_clock::now();
              const auto duration =
                  std::chrono::duration_cast<std::chrono::milliseconds>(finished - started);
              {
                std::scoped_lock lock(mutex_);
                latest_ = std::move(resolution);
                latest_started_at_ = started;
                latest_at_ = finished;
                pending_ = true;
              }
              if (duration >= std::chrono::milliseconds(250) &&
                  finished - last_slow_log >= std::chrono::seconds(30)) {
                core::DiagnosticRecord record{};
                record.event = core::DiagnosticEventType::Error;
                record.code = "TARGET_RESOLVE_SLOW";
                record.status = std::to_string(duration.count()) + "ms";
                (void)log.append(record);
                last_slow_log = finished;
              }
            } catch (...) {
              // Never keep a stale exit target after a failed resolution.
              std::scoped_lock lock(mutex_);
              latest_ = {};
              latest_started_at_ = started;
              latest_at_ = std::chrono::steady_clock::now();
              pending_ = true;
            }
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(250),
                           [this] { return stop_.load() || refresh_requested_; });
            refresh_requested_ = false;
          }
        }) {}
  ~ForegroundResolutionWorker() {
    stop();
  }
  ForegroundResolutionWorker(const ForegroundResolutionWorker&) = delete;
  ForegroundResolutionWorker& operator=(const ForegroundResolutionWorker&) = delete;
  void request_stop() {
    stop_ = true;
    wake_.notify_all();
  }
  void stop() {
    request_stop();
    if (thread_.joinable())
      thread_.join();
  }
  bool take(platform::windows::TargetResolution& resolution,
            std::chrono::steady_clock::time_point& started_at,
            std::chrono::steady_clock::time_point& completed_at) {
    std::scoped_lock lock(mutex_);
    if (!pending_)
      return false;
    resolution = std::move(latest_);
    started_at = latest_started_at_;
    completed_at = latest_at_;
    pending_ = false;
    return true;
  }
  bool autolaunch_disabled() const {
    return autolaunch_disabled_.load();
  }
  void request_refresh() {
    {
      std::scoped_lock lock(mutex_);
      if (stop_)
        return;
      refresh_requested_ = true;
    }
    wake_.notify_one();
  }

 private:
  std::atomic_bool stop_ = false;
  std::atomic_bool autolaunch_disabled_ = false;
  std::mutex mutex_;
  std::condition_variable wake_;
  platform::windows::TargetResolution latest_{};
  std::chrono::steady_clock::time_point latest_started_at_{};
  std::chrono::steady_clock::time_point latest_at_{};
  bool pending_ = false;
  bool refresh_requested_ = false;
  std::thread thread_;
};

core::Vec3 cv(Vec3 v) {
  return {v.x, v.y, v.z};
}
std::wstring onoff(bool value, core::Language language) {
  return widen(core::localize(value ? core::StringId::On : core::StringId::Off, language));
}
core::StringId game_reason(platform::windows::CurrentVrGameStatus status) {
  using S = platform::windows::CurrentVrGameStatus;
  switch (status) {
    case S::Available:
      return core::StringId::Ready;
    case S::ApplicationsUnavailable:
      return core::StringId::NoApplicationsInterface;
    case S::Resolving:
      return core::StringId::ResolvingCurrentGame;
    case S::SceneNotRunning:
      return core::StringId::NoSceneApplication;
    case S::SceneProcessUnavailable:
      return core::StringId::SceneProcessUnavailable;
    case S::SelfProcessRejected:
      return core::StringId::SelfProcessRejected;
    case S::ApplicationKeyUnavailable:
      return core::StringId::ApplicationKeyUnavailable;
    case S::ApplicationMetadataUnavailable:
      return core::StringId::ApplicationMetadataUnavailable;
    case S::UnsafeApplication:
      return core::StringId::UnsafeApplication;
  }
  return core::StringId::Unavailable;
}
core::StringId target_reason(platform::windows::ExitTargetStatus status) {
  using S = platform::windows::ExitTargetStatus;
  switch (status) {
    case S::Available:
      return core::StringId::Ready;
    case S::CurrentGameUnavailable:
      return core::StringId::CurrentGameUnavailable;
    case S::ProcessUnavailable:
      return core::StringId::ProcessUnavailable;
    case S::ProcessAccessDenied:
      return core::StringId::ProcessAccessDenied;
    case S::ProcessIdentityUnavailable:
      return core::StringId::ProcessIdentityUnavailable;
    case S::ProcessPathUnavailable:
      return core::StringId::ProcessPathUnavailable;
    case S::UnsafeProcess:
      return core::StringId::UnsafeProcess;
  }
  return core::StringId::Unavailable;
}
}  // namespace
RuntimeApp::RuntimeApp()
    : settings_store_(platform::windows::settings_path()),
      log_(platform::windows::log_directory() / L"runtime.log") {
  auto loaded = settings_store_.load();
  settings_ = loaded.settings;
  gesture_ = core::GestureDetector(core::gesture_config(settings_.gesture_sensitivity));
  if (loaded.status != core::SettingsLoadStatus::Loaded) {
    std::string error;
    if (!settings_store_.save(settings_, &error)) {
      core::DiagnosticRecord failure{};
      failure.event = core::DiagnosticEventType::Error;
      failure.code = error;
      (void)log_.append(failure);
    }
    core::DiagnosticRecord recovery{};
    recovery.event = core::DiagnosticEventType::SettingsRecovered;
    recovery.code = loaded.diagnostic_code;
    (void)log_.append(recovery);
  }
  flow_ = std::make_unique<core::FlowStateMachine>(
      std::chrono::seconds(settings_.control_timeout_seconds));
}
RuntimeApp::~RuntimeApp() {
  // run() joins its local workers before these OpenVR interfaces are released.
  process_.clear();
  if (overlays_)
    overlays_->shutdown();
  vr_.shutdown();
  log_.stop_async();
}
core::TargetIdentity RuntimeApp::core_target(const RuntimeTarget& t) const {
  return {t.pid, t.creation_token, narrow(t.executable_path), narrow(t.display_name)};
}
core::Prerequisites RuntimeApp::prerequisites(const TrackingFrame& f,
                                              const platform::windows::TargetResolution& resolution,
                                              bool dashboard_visible) const {
  const auto& configured = settings_.configured_hand == core::Hand::Left ? f.left : f.right;
  const auto& opposite = settings_.configured_hand == core::Hand::Left ? f.right : f.left;
  core::Prerequisites p{};
  p.current_vr_game = resolution.game.has_value() && !dashboard_visible;
  p.hardware_available = f.headset_connected && (f.left.connected || f.right.connected);
  p.headset_tracked = f.headset_tracked && !dashboard_visible;
  p.configured_hand_tracked = configured.tracked && !dashboard_visible;
  p.opposite_pointer_available = opposite.pointer_available() && !dashboard_visible;
  if (p.current_vr_game && resolution.target)
    p.exit_target = core_target(*resolution.target);
  return p;
}
std::wstring RuntimeApp::text(core::StringId id) const {
  return widen(core::localize(id, language_));
}
void RuntimeApp::log_overlay_error(std::chrono::steady_clock::time_point now) {
  std::string detail = overlays_->take_error();
  if (detail.empty())
    detail = "unknown overlay failure";
  if (detail != last_overlay_error_ || now - last_overlay_error_at_ >= std::chrono::seconds(30)) {
    core::DiagnosticRecord record{};
    record.event = core::DiagnosticEventType::Error;
    record.code = "OVERLAY_OPERATION_FAILED";
    record.status = detail;
    (void)log_.append(record);
    last_overlay_error_ = detail;
    last_overlay_error_at_ = now;
  }
}
void RuntimeApp::abort_overlay_flow(std::chrono::steady_clock::time_point now,
                                    const TrackingFrame& frame) {
  log_overlay_error(now);
  apply(flow_->on_overlay_failure("OVERLAY_PRESENT_FAILED"), now, frame);
}
std::optional<core::SettingsChange> RuntimeApp::dashboard_input(const core::DashboardAction action,
                                                                const core::Settings& base) {
  auto candidate = base;
  if (!core::apply_dashboard_action(candidate, action))
    return std::nullopt;
  const char* name = nullptr;
  std::string value;
  if (candidate.configured_hand != base.configured_hand) {
    name = "configured_hand";
    value = core::to_string(candidate.configured_hand);
  } else if (candidate.countdown_seconds != base.countdown_seconds) {
    name = "countdown_seconds";
    value = std::to_string(candidate.countdown_seconds);
  } else if (candidate.gesture_sensitivity != base.gesture_sensitivity) {
    name = "gesture_sensitivity";
    value = core::to_string(candidate.gesture_sensitivity);
  } else if (candidate.control_timeout_seconds != base.control_timeout_seconds) {
    name = "control_timeout_seconds";
    value = std::to_string(candidate.control_timeout_seconds);
  } else if (candidate.countdown_sound != base.countdown_sound) {
    name = "countdown_sound";
    value = candidate.countdown_sound ? "true" : "false";
  } else if (candidate.language != base.language) {
    name = "language";
    value = core::to_string(candidate.language);
  }
  if (!name)
    return std::nullopt;
  return core::SettingsChange{candidate, name, std::move(value)};
}
void RuntimeApp::apply_settings(const core::Settings& next) {
  const bool hand_changed = next.configured_hand != settings_.configured_hand;
  const bool sensitivity_changed = next.gesture_sensitivity != settings_.gesture_sensitivity;
  const bool timeout_changed = next.control_timeout_seconds != settings_.control_timeout_seconds;
  const bool language_changed = next.language != settings_.language;
  settings_ = next;
  if (sensitivity_changed)
    gesture_ = core::GestureDetector(core::gesture_config(settings_.gesture_sensitivity));
  else if (hand_changed)
    gesture_.reset();
  if (timeout_changed)
    flow_->set_control_timeout(std::chrono::seconds(settings_.control_timeout_seconds));
  if (language_changed) {
    language_ = core::resolve_language(settings_.language, steam_language_, os_language_);
    thumbnail_dirty_ = true;
  }
}
void RuntimeApp::update_dashboard(const platform::windows::TargetResolution& resolution,
                                  const TrackingFrame& f) {
  if (!overlays_)
    return;
  const auto& configured = settings_.configured_hand == core::Hand::Left ? f.left : f.right;
  const auto& opposite = settings_.configured_hand == core::Hand::Left ? f.right : f.left;
  const auto now = std::chrono::steady_clock::now();
  const auto snapshot = flow_->snapshot(now);
  platform::openvr::DashboardSurfaceModel model{};
  model.settings_title = text(core::StringId::SettingsTitle);
  model.diagnostics_title = text(core::StringId::DiagnosticsTitle);
  model.help_title = text(core::StringId::QuickGuide);
  if (save_notice_success_ && now < save_notice_until_) {
    model.notice = text(*save_notice_success_ ? core::StringId::SettingsSaved
                                             : core::StringId::SettingsSaveFailed);
    model.notice_tone = *save_notice_success_
                            ? platform::openvr::DashboardNoticeTone::Success
                            : platform::openvr::DashboardNoticeTone::Error;
  }
  model.settings = {
      {core::DashboardSettingRow::ConfiguredHand, text(core::StringId::ConfiguredHand), L""},
      {core::DashboardSettingRow::GestureSensitivity, text(core::StringId::GestureSensitivity),
       L""},
      {core::DashboardSettingRow::CountdownDuration, text(core::StringId::CountdownDuration), L""},
      {core::DashboardSettingRow::ControlTimeout, text(core::StringId::ControlTimeout),
       std::to_wstring(settings_.control_timeout_seconds) + text(core::StringId::SecondsUnit)},
      {core::DashboardSettingRow::CountdownSound, text(core::StringId::CountdownSound), L""},
      {core::DashboardSettingRow::Language, text(core::StringId::LanguageLabel), L""}};
  model.buttons = core::dashboard_buttons(settings_);
  for (auto& button : model.buttons) {
    switch (button.action) {
      case core::DashboardAction::SetLeftHand:
        button.label = text(core::StringId::Left);
        break;
      case core::DashboardAction::SetRightHand:
        button.label = text(core::StringId::Right);
        break;
      case core::DashboardAction::SetSensitivityLow:
        button.label = text(core::StringId::SensitivityLow);
        break;
      case core::DashboardAction::SetSensitivityNormal:
        button.label = text(core::StringId::SensitivityNormal);
        break;
      case core::DashboardAction::SetSensitivityHigh:
        button.label = text(core::StringId::SensitivityHigh);
        break;
      case core::DashboardAction::SetCountdown1:
        button.label = L"1" + text(core::StringId::SecondsUnit);
        break;
      case core::DashboardAction::SetCountdown3:
        button.label = L"3" + text(core::StringId::SecondsUnit);
        break;
      case core::DashboardAction::SetCountdown5:
        button.label = L"5" + text(core::StringId::SecondsUnit);
        break;
      case core::DashboardAction::SetCountdown10:
        button.label = L"10" + text(core::StringId::SecondsUnit);
        break;
      case core::DashboardAction::SetCountdown15:
        button.label = L"15" + text(core::StringId::SecondsUnit);
        break;
      case core::DashboardAction::DecreaseControlTimeout:
        button.label = L"−";
        break;
      case core::DashboardAction::IncreaseControlTimeout:
        button.label = L"+";
        break;
      case core::DashboardAction::EnableCountdownSound:
        button.label = onoff(true, language_);
        break;
      case core::DashboardAction::DisableCountdownSound:
        button.label = onoff(false, language_);
        break;
      case core::DashboardAction::SetLanguageAuto:
        button.label = text(core::StringId::AutoLanguage);
        break;
      case core::DashboardAction::SetLanguageEnglish:
        button.label = text(core::StringId::EnglishLanguage);
        break;
      case core::DashboardAction::SetLanguageChinese:
        button.label = text(core::StringId::ChineseLanguage);
        break;
    }
  }
  const bool game_available = resolution.game.has_value();
  const bool target_available = resolution.target.has_value();
  const std::wstring game_value =
      game_available ? resolution.game->display_name : text(game_reason(resolution.game_status));
  const std::wstring target_value = target_available
                                        ? resolution.target->display_name
                                        : text(target_reason(resolution.target_status));
  std::wstring last = widen(core::localize_last_exit(snapshot.last_exit, language_));
  if (snapshot.last_exit == core::LastExitStatus::Succeeded && last_successful_exit_at_)
    last += L"  ·  " + local_timestamp(*last_successful_exit_at_);
  model.statuses = {{text(core::StringId::CurrentVrGame), game_value,
                     game_available ? platform::openvr::DashboardStatusTone::Available
                                    : platform::openvr::DashboardStatusTone::Neutral},
                    {text(core::StringId::ExitTarget), target_value,
                     target_available ? platform::openvr::DashboardStatusTone::Available
                                      : platform::openvr::DashboardStatusTone::Neutral},
                    {text(core::StringId::HandTracking),
                     configured.tracked ? text(core::StringId::ControllerTracked)
                                        : text(core::StringId::ControllerNotTracked),
                     configured.tracked ? platform::openvr::DashboardStatusTone::Available
                                        : platform::openvr::DashboardStatusTone::Neutral},
                    {text(core::StringId::OppositePointer),
                     opposite.pointer_available() ? text(core::StringId::ControllerTracked)
                                                  : text(core::StringId::ControllerNotTracked),
                     opposite.pointer_available() ? platform::openvr::DashboardStatusTone::Available
                                                  : platform::openvr::DashboardStatusTone::Neutral},
                    {text(core::StringId::LastExit), last,
                     snapshot.last_exit == core::LastExitStatus::Succeeded
                         ? platform::openvr::DashboardStatusTone::Available
                         : platform::openvr::DashboardStatusTone::Neutral}};
  model.help_lines = {text(core::StringId::HelpGesture), text(core::StringId::HelpOppositeHand),
                      text(core::StringId::HelpForceClose)};
  const auto submit_started = std::chrono::steady_clock::now();
  const bool submitted = overlays_->update_dashboard(model);
  const auto submit_finished = std::chrono::steady_clock::now();
  const auto submit_duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(submit_finished - submit_started);
  if (submit_duration >= std::chrono::milliseconds(32) &&
      submit_finished - last_slow_dashboard_at_ >= std::chrono::seconds(5)) {
    core::DiagnosticRecord record{};
    record.event = core::DiagnosticEventType::Error;
    record.code = "DASHBOARD_SUBMIT_SLOW";
    record.status = std::to_string(submit_duration.count()) + "ms";
    (void)log_.append(record);
    last_slow_dashboard_at_ = submit_finished;
  }
  if (!submitted)
    log_overlay_error(now);
}
void RuntimeApp::apply(core::Effects effects, std::chrono::steady_clock::time_point now,
                       const TrackingFrame& f) {
  for (size_t i = 0; i < effects.size(); ++i) {
    const auto& e = effects[i];
    auto append = [&](core::Effects more) {
      effects.insert(effects.end(), std::make_move_iterator(more.begin()),
                     std::make_move_iterator(more.end()));
    };
    switch (e.type) {
      case core::EffectType::ShowIdleControl:
        if (!overlays_->show_wrist(
                settings_.configured_hand, f,
                {text(core::StringId::ConfirmTitle), L"", text(core::StringId::ExitGame),
                 text(core::StringId::Dismiss), L"", 0.0f, platform::openvr::SurfaceMode::Prompt,
                 1.0f, settings_.countdown_seconds})) {
          abort_overlay_flow(now, f);
          return;
        }
        break;
      case core::EffectType::ShowConfirmation: {
        if (!overlays_->show_wrist(
                settings_.configured_hand, f,
                {text(core::StringId::ConfirmTitle), L"", text(core::StringId::Confirm),
                 text(core::StringId::Dismiss), L"", 0.0f, platform::openvr::SurfaceMode::Confirm,
                 static_cast<float>(flow_->snapshot(now).control_timeout_progress),
                 settings_.countdown_seconds})) {
          abort_overlay_flow(now, f);
          return;
        }
        break;
      }
      case core::EffectType::UpdateControlTimeout:
        if (!overlays_->update_wrist_timeout(static_cast<float>(e.progress))) {
          abort_overlay_flow(now, f);
          return;
        }
        break;
      case core::EffectType::HideControl:
        if (!overlays_->hide_wrist())
          log_overlay_error(now);
        break;
      case core::EffectType::ShowCountdownControl:
      case core::EffectType::UpdateCountdownControl: {
        double seconds = std::max(0.0, e.remaining.count() / 1000.0);
        const int display_seconds = std::max(1, static_cast<int>(std::ceil(seconds)));
        if (!overlays_->show_wrist(
                settings_.configured_hand, f,
                {L"", L"", L"", text(core::StringId::Dismiss), L"", static_cast<float>(e.progress),
                 platform::openvr::SurfaceMode::Countdown, 0.0f, display_seconds})) {
          abort_overlay_flow(now, f);
          return;
        }
        int beep = static_cast<int>(std::ceil(seconds));
        if (beep != last_beep_second_ && beep > 0) {
          sound_.tick(settings_.countdown_sound);
          last_beep_second_ = beep;
        }
        break;
      }
      case core::EffectType::ShowFailureHud:
        if (!overlays_->show_hud(
                {text(core::StringId::ExitFailed), text(core::StringId::CloseManually)}))
          log_overlay_error(now);
        break;
      case core::EffectType::HideHud:
        if (!overlays_->hide_hud())
          log_overlay_error(now);
        last_beep_second_ = -1;
        break;
      case core::EffectType::RequestGracefulClose: {
        const auto validation = locked_native_ ? targets_->revalidate(*locked_native_)
                                               : platform::windows::TargetValidation::Mismatch;
        if (validation == platform::windows::TargetValidation::Busy) {
          append(flow_->on_graceful_result(now, core::CloseRequestResult::Failed,
                                           "EXIT_REVALIDATION_BUSY"));
          break;
        }
        if (validation != platform::windows::TargetValidation::Matching) {
          append(flow_->on_target_status(now, core::TargetStatus::IdentityMismatch));
          break;
        }
        bool ok = process_.request_graceful_close(*locked_native_, now);
        if (!ok && process_.status() == platform::windows::CloseStatus::IdentityInvalid) {
          append(flow_->on_target_status(now, core::TargetStatus::IdentityMismatch));
          break;
        }
        append(flow_->on_graceful_result(
            now, ok ? core::CloseRequestResult::Accepted : core::CloseRequestResult::Failed,
            std::string(process_.error_code())));
        break;
      }
      case core::EffectType::RequestForceClose: {
        const auto validation = locked_native_ ? targets_->revalidate(*locked_native_)
                                               : platform::windows::TargetValidation::Mismatch;
        if (validation == platform::windows::TargetValidation::Busy) {
          append(flow_->on_force_result(now, core::CloseRequestResult::Failed,
                                        "EXIT_REVALIDATION_BUSY"));
          break;
        }
        if (validation != platform::windows::TargetValidation::Matching) {
          append(flow_->on_target_status(now, core::TargetStatus::IdentityMismatch));
          break;
        }
        auto status = process_.force_close(now);
        if (status == platform::windows::CloseStatus::IdentityInvalid)
          append(flow_->on_target_status(now, core::TargetStatus::IdentityMismatch));
        else if (status == platform::windows::CloseStatus::ExitedGracefully)
          append(flow_->on_target_status(now, core::TargetStatus::Exited));
        else if (status == platform::windows::CloseStatus::ForcePending)
          append(flow_->on_force_result(now, core::CloseRequestResult::Accepted));
        else
          append(flow_->on_force_result(now,
                                        status == platform::windows::CloseStatus::ForceSucceeded
                                            ? core::CloseRequestResult::TargetExited
                                            : core::CloseRequestResult::Failed,
                                        "EXIT_TERMINATE_PROCESS_FAILED"));
        break;
      }
      case core::EffectType::Diagnostic: {
        core::DiagnosticRecord record{};
        record.event = e.code.rfind("EXIT_", 0) == 0 ? core::DiagnosticEventType::ExitResult
                                                     : core::DiagnosticEventType::StateTransition;
        record.code = e.code;
        record.from_state = std::string(core::to_string(e.from_state));
        record.to_state = std::string(core::to_string(e.to_state));
        record.details["last_exit"] = std::string(core::to_string(flow_->snapshot(now).last_exit));
        if (e.target)
          record.target_name = e.target->display_name;
        (void)log_.append(record);
        if (core::completes_exit_flow(e)) {
          if (flow_->snapshot(now).last_exit == core::LastExitStatus::Succeeded)
            last_successful_exit_at_ = std::chrono::system_clock::now();
          process_.clear();
          locked_native_.reset();
        }
        break;
      }
    }
  }
}
int RuntimeApp::run() {
  SteamVrServerMonitor server;
  if (!server.attach() || !server.alive()) {
    (void)log_.append({core::DiagnosticEventType::Lifecycle, "STEAMVR_SERVER_UNAVAILABLE"});
    return 0;
  }
  const uint64_t creation_token = process_creation_token(GetCurrentProcess());
  if (creation_token == 0) {
    (void)log_.append({core::DiagnosticEventType::Error, "STOP_SIGNAL_INIT_FAILED"});
    return 1;
  }
  const std::wstring stop_name = L"Local\\SteamVRBailOut.Stop." +
                                 std::to_wstring(GetCurrentProcessId()) + L"." +
                                 std::to_wstring(creation_token);
  ScopedHandle stop_event(CreateEventW(nullptr, TRUE, FALSE, stop_name.c_str()));
  if (!stop_event.get()) {
    (void)log_.append({core::DiagnosticEventType::Error, "STOP_SIGNAL_INIT_FAILED"});
    return 1;
  }
  if (!vr_.initialize()) {
    core::DiagnosticRecord record{core::DiagnosticEventType::Error, "OPENVR_INIT_FAILED"};
    record.status = vr_.error();
    (void)log_.append(record);
    return 1;
  }
  if (!vr_.applications()->IsApplicationInstalled(kAppKey) ||
      !vr_.applications()->GetApplicationAutoLaunch(kAppKey)) {
    (void)log_.append({core::DiagnosticEventType::Lifecycle, "AUTO_LAUNCH_DISABLED"});
    return 0;
  }
  if (!server.alive()) {
    (void)log_.append({core::DiagnosticEventType::Lifecycle, "STEAMVR_SERVER_EXITED"});
    return 0;
  }
  // Resolve Auto before the first texture is submitted. Otherwise a visible
  // Dashboard briefly receives the default English surface.
  std::array<wchar_t, LOCALE_NAME_MAX_LENGTH> locale{};
  const LCID ui_locale = MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT);
  if (LCIDToLocaleName(ui_locale, locale.data(), static_cast<int>(locale.size()), 0) ||
      GetUserDefaultLocaleName(locale.data(), static_cast<int>(locale.size())))
    os_language_ = narrow(locale.data());
  std::array<char, 64> steam{};
  vr::EVRSettingsError language_error{};
  if (vr::VRSettings()) {
    vr::VRSettings()->GetString("steamvr", "language", steam.data(),
                                static_cast<uint32_t>(steam.size()), &language_error);
    if (language_error != vr::VRSettingsError_None)
      vr::VRSettings()->GetString("steam", "language", steam.data(),
                                  static_cast<uint32_t>(steam.size()), &language_error);
  }
  if (language_error == vr::VRSettingsError_None)
    steam_language_ = steam.data();
  language_ = core::resolve_language(settings_.language, steam_language_, os_language_);
  // Create the Dashboard immediately after language discovery. SteamVR may
  // start an overlay before discovering devices, so hardware gating remains
  // in the normal loop below.
  overlays_ = std::make_unique<platform::openvr::OverlayManager>(vr_.overlays(), &log_);
  if (!overlays_->initialize()) {
    core::DiagnosticRecord record{};
    record.event = core::DiagnosticEventType::Error;
    record.code = "OVERLAY_INIT_FAILED";
    record.status = overlays_->error();
    (void)log_.append(record);
    return 1;
  }
  std::chrono::steady_clock::time_point last_slow_visibility_at{};
  const auto dashboard_open = [this, &last_slow_visibility_at] {
    const auto started = std::chrono::steady_clock::now();
    const bool global_visible = vr_.overlays()->IsDashboardVisible();
    const auto global_finished = std::chrono::steady_clock::now();
    const bool visible = global_visible || overlays_->dashboard_visible();
    const auto finished = std::chrono::steady_clock::now();
    if (finished - started >= std::chrono::milliseconds(32) &&
        finished - last_slow_visibility_at >= std::chrono::seconds(5)) {
      core::DiagnosticRecord record{core::DiagnosticEventType::Error,
                                    "DASHBOARD_VISIBILITY_QUERY_SLOW"};
      record.details = {
          {"global_ms", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                           global_finished - started)
                                           .count())},
          {"overlay_ms", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            finished - global_finished)
                                            .count())}};
      (void)log_.append(record);
      last_slow_visibility_at = finished;
    }
    return visible;
  };
  update_dashboard(latest_resolution_, {});
  targets_ = std::make_unique<platform::windows::ExitTargetResolver>(vr_.applications());
  if (!overlays_->update_thumbnail(text(core::StringId::ThumbnailTitle),
                                   text(core::StringId::ThumbnailDescription),
                                   std::string(core::localize(core::StringId::AppName, language_))))
    log_overlay_error(std::chrono::steady_clock::now());
  core::DiagnosticRecord started{};
  started.event = core::DiagnosticEventType::Lifecycle;
  started.code = "RUNTIME_STARTED";
#ifdef NDEBUG
  started.details["build"] = "release";
#else
  started.details["build"] = "debug";
#endif
  started.details["tracking_source"] = "GetDeviceToAbsoluteTrackingPose(standing,0)";
  started.details["configured_hand"] = std::string(core::to_string(settings_.configured_hand));
  (void)log_.append(started);
  log_.start_async();
  ForegroundResolutionWorker target_worker(*targets_, log_);
  core::SettingsPersistenceWorker settings_writer(settings_store_.path());
  // The runtime configuration is the authoritative interactive state. Durable
  // persistence stays off the input/render critical path and reports failures
  // without rolling a newer in-memory setting back to an older disk snapshot.
  const auto consume_save_results = [&] {
    auto results = settings_writer.take_results();
    bool save_notice_changed = false;
    const auto confirmed_at = std::chrono::steady_clock::now();
    for (auto& result : results) {
      core::DiagnosticRecord record{};
      record.event = result.saved ? core::DiagnosticEventType::SettingsChanged
                                  : core::DiagnosticEventType::Error;
      record.code = result.saved           ? "SETTINGS_SAVED"
                    : result.error.empty() ? "SETTINGS_WRITE_FAILED"
                                           : result.error;
      if (result.saved) {
        record.setting_name = std::move(result.change.name);
        record.setting_value = std::move(result.change.value);
      }
      save_notice_success_ = result.saved;
      save_notice_until_ = confirmed_at + std::chrono::seconds(8);
      save_notice_changed = true;
      if (result.change.requested_at != std::chrono::steady_clock::time_point{}) {
        record.details["queue_wait_ms"] =
            std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                               result.started_at - result.change.requested_at)
                               .count());
        record.details["write_ms"] =
            std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                               result.completed_at - result.started_at)
                               .count());
        record.details["persistence_ms"] =
            std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                               confirmed_at - result.change.requested_at)
                               .count());
      }
      (void)log_.append(record);
    }
    return save_notice_changed;
  };
  const auto started_at = std::chrono::steady_clock::now();
  auto next_server_check = started_at;
  auto last_dashboard_refresh_at = started_at;
  std::optional<std::chrono::steady_clock::time_point> no_devices_since;
  TrackingFrame last_frame{};
  bool dashboard_was_open = dashboard_open();
  std::uint32_t observed_scene_pid = 0;
  std::chrono::steady_clock::time_point latest_resolution_at{};
  std::optional<RuntimeTarget> pending_confirmation;
  std::chrono::steady_clock::time_point confirmation_requested_at{};
  int observed_scene_state = static_cast<int>(vr::EVRSceneApplicationState_None);
  std::chrono::steady_clock::time_point scene_event_at{};
  auto last_ui_hover_sound_at = std::chrono::steady_clock::time_point{};
  auto last_slow_loop_at = std::chrono::steady_clock::time_point{};
  auto last_slow_system_events_at = std::chrono::steady_clock::time_point{};
  auto last_slow_input_at = std::chrono::steady_clock::time_point{};
  auto last_input_backlog_at = std::chrono::steady_clock::time_point{};
  auto last_slow_interaction_at = std::chrono::steady_clock::time_point{};
  auto last_slow_tracking_at = std::chrono::steady_clock::time_point{};
  auto last_target_log_at = std::chrono::steady_clock::time_point{};
  std::string last_target_signature;
  TimerResolution timer_resolution;
  while (true) {
    if (!stop_ && WaitForSingleObject(stop_event.get(), 0) == WAIT_OBJECT_0) {
      stop_ = true;
      (void)log_.append({core::DiagnosticEventType::Lifecycle, "RUNTIME_STOP_REQUESTED"});
    }
    if (stop_ && flow_->state() != core::FlowState::Exiting)
      break;
    const auto loop_start = std::chrono::steady_clock::now();
    // A visible panel proves the Dashboard is open even when SteamVR's global
    // visibility query has not caught up. Publish that change before the
    // periodic checks and diagnostic writes in this loop.
    const bool dashboard_at_start = dashboard_open();
    if (dashboard_at_start != dashboard_was_open) {
      dashboard_was_open = dashboard_at_start;
      core::DiagnosticRecord record{};
      record.event = core::DiagnosticEventType::StateTransition;
      record.code = "DASHBOARD_VISIBILITY_CHANGED";
      record.debug_only = true;
      record.available = dashboard_at_start;
      record.details["steamvr_global_visible"] =
          vr_.overlays()->IsDashboardVisible() ? "true" : "false";
      record.details["overlay_visible"] = overlays_->dashboard_visible() ? "true" : "false";
      (void)log_.append(record);
      target_worker.request_refresh();
      update_dashboard(latest_resolution_, last_frame);
      last_dashboard_refresh_at = std::chrono::steady_clock::now();
    }
    if (loop_start >= next_server_check) {
      if (!server.alive()) {
        (void)log_.append({core::DiagnosticEventType::Lifecycle, "STEAMVR_SERVER_EXITED"});
        break;
      }
      next_server_check = loop_start + std::chrono::milliseconds(250);
    }
    if (target_worker.autolaunch_disabled()) {
      if (!stop_)
        (void)log_.append({core::DiagnosticEventType::Lifecycle, "AUTO_LAUNCH_DISABLED"});
      stop_ = true;
      if (flow_->state() != core::FlowState::Exiting)
        break;
    }
    vr::VREvent_t system_event{};
    bool vr_quit = false;
    bool scene_event = false;
    bool scene_may_have_changed = false;
    const auto system_events_started = std::chrono::steady_clock::now();
    for (size_t count = 0;
         count < 256 && vr_.system()->PollNextEvent(&system_event, sizeof(system_event)); ++count) {
      if (system_event.eventType == vr::VREvent_Quit) {
        vr_.system()->AcknowledgeQuit_Exiting();
        vr_quit = true;
        break;
      }
      switch (system_event.eventType) {
        case vr::VREvent_SceneApplicationChanged:
        case vr::VREvent_SceneApplicationStateChanged:
        case vr::VREvent_ProcessConnected:
        case vr::VREvent_ProcessQuit:
          scene_may_have_changed = true;
          scene_event = true;
          break;
        case vr::VREvent_ApplicationListUpdated:
          scene_event = true;
          break;
        default:
          break;
      }
    }
    const auto system_events_finished = std::chrono::steady_clock::now();
    if (system_events_finished - system_events_started >= std::chrono::milliseconds(32) &&
        system_events_finished - last_slow_system_events_at >= std::chrono::seconds(5)) {
      core::DiagnosticRecord record{core::DiagnosticEventType::Error, "SYSTEM_EVENT_POLL_SLOW"};
      record.status = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         system_events_finished - system_events_started)
                                         .count()) +
                      "ms";
      (void)log_.append(record);
      last_slow_system_events_at = system_events_finished;
    }
    if (vr_quit)
      break;
    const auto now = std::chrono::steady_clock::now();
    if (scene_may_have_changed) {
      // An in-flight resolver result predating this event must not reinstate
      // a target from the previous scene.
      scene_event_at = now;
      latest_resolution_ = {};
      latest_resolution_at = {};
      latest_resolution_.game_status = platform::windows::CurrentVrGameStatus::Resolving;
    }
    if (scene_event)
      target_worker.request_refresh();
    platform::windows::TargetResolution resolved{};
    std::optional<platform::windows::TargetResolution> confirmation_resolution;
    std::chrono::steady_clock::time_point resolved_started_at{};
    std::chrono::steady_clock::time_point resolved_at{};
    if (target_worker.take(resolved, resolved_started_at, resolved_at)) {
      const bool current_scene = resolved_started_at >= scene_event_at;
      if (current_scene && (resolved.scene_state != observed_scene_state ||
                            resolved.scene_pid != observed_scene_pid)) {
        core::DiagnosticRecord scene_record{};
        scene_record.event = core::DiagnosticEventType::AvailabilityChanged;
        scene_record.code = "SCENE_CHANGED";
        scene_record.debug_only = true;
        scene_record.details = {{"previous_scene_pid", std::to_string(observed_scene_pid)},
                                {"scene_pid", std::to_string(resolved.scene_pid)},
                                {"scene_state", std::to_string(resolved.scene_state)}};
        (void)log_.append(scene_record);
        observed_scene_state = resolved.scene_state;
        observed_scene_pid = resolved.scene_pid;
        latest_resolution_ = {};
        latest_resolution_at = {};
        latest_resolution_.game_status =
            observed_scene_pid ? platform::windows::CurrentVrGameStatus::Resolving
                               : platform::windows::CurrentVrGameStatus::SceneNotRunning;
      }
      // A result from a query begun before the click is not confirmation evidence.
      if (current_scene && pending_confirmation && resolved_started_at >= confirmation_requested_at)
        confirmation_resolution = resolved;
      const std::string signature = std::to_string(resolved.scene_pid) + ":" +
                                    platform::windows::diagnostic_code(resolved.game_status) + ":" +
                                    platform::windows::diagnostic_code(resolved.target_status) +
                                    ":" + (current_scene ? "current" : "stale");
      if (signature != last_target_signature ||
          now - last_target_log_at >= std::chrono::seconds(5)) {
        core::DiagnosticRecord record{};
        record.event = core::DiagnosticEventType::AvailabilityChanged;
        record.code = "TARGET_RESOLUTION_RECEIVED";
        record.debug_only = true;
        record.current_game_status = platform::windows::diagnostic_code(resolved.game_status);
        record.exit_target_status = platform::windows::diagnostic_code(resolved.target_status);
        record.details = {
            {"resolved_scene_pid", std::to_string(resolved.scene_pid)},
            {"observed_scene_pid", std::to_string(observed_scene_pid)},
            {"scene_matches", current_scene ? "true" : "false"},
            {"result_age_ms", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 std::chrono::steady_clock::now() - resolved_at)
                                                 .count())},
            {"has_game", resolved.game ? "true" : "false"},
            {"has_target", resolved.target ? "true" : "false"}};
        (void)log_.append(record);
        last_target_signature = signature;
        last_target_log_at = now;
      }
      if (current_scene &&
          std::chrono::steady_clock::now() - resolved_at < kMaxTargetResolutionAge) {
        latest_resolution_ = std::move(resolved);
        if (latest_resolution_.scene_pid)
          latest_resolution_at = resolved_at;
      }
    }
    if (latest_resolution_at != std::chrono::steady_clock::time_point{} &&
        std::chrono::steady_clock::now() - latest_resolution_at >= kMaxTargetResolutionAge) {
      latest_resolution_at = {};
      latest_resolution_ = {};
      latest_resolution_.scene_pid = observed_scene_pid;
      latest_resolution_.game_status =
          observed_scene_pid ? platform::windows::CurrentVrGameStatus::Resolving
                             : platform::windows::CurrentVrGameStatus::SceneNotRunning;
      (void)log_.append({core::DiagnosticEventType::Error, "TARGET_RESOLUTION_STALE"});
      target_worker.request_refresh();
    }
    bool save_notice_changed = consume_save_results();
    const auto input_started = std::chrono::steady_clock::now();
    const auto dashboard_events = overlays_->poll_dashboard_events();
    const auto input_finished = std::chrono::steady_clock::now();
    if (input_finished - input_started >= std::chrono::milliseconds(32) &&
        input_finished - last_slow_input_at >= std::chrono::seconds(5)) {
      core::DiagnosticRecord record{core::DiagnosticEventType::Error, "DASHBOARD_INPUT_POLL_SLOW"};
      record.status = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         input_finished - input_started)
                                         .count()) +
                      "ms";
      (void)log_.append(record);
      last_slow_input_at = input_finished;
    }
    if (dashboard_events.max_event_age_seconds >= 0.05F &&
        input_finished - last_input_backlog_at >= std::chrono::seconds(5)) {
      core::DiagnosticRecord record{core::DiagnosticEventType::Error, "DASHBOARD_EVENT_BACKLOG"};
      record.status =
          std::to_string(static_cast<int>(dashboard_events.max_event_age_seconds * 1000.0F)) + "ms";
      (void)log_.append(record);
      last_input_backlog_at = input_finished;
    }
    bool settings_changed = false;
    if (!overlays_->error().empty())
      log_overlay_error(now);
    else {
      if (dashboard_events.entered_button &&
          now - last_ui_hover_sound_at >= std::chrono::milliseconds(150)) {
        sound_.ui_hover();
        last_ui_hover_sound_at = now;
      }
      for (const auto action : dashboard_events.actions) {
        auto change = dashboard_input(action, settings_);
        if (!change)
          continue;
        change->requested_at = std::chrono::steady_clock::now();
        const auto next = change->settings;
        if (settings_writer.enqueue(std::move(*change))) {
          // This is the actual configuration commit, not a speculative visual
          // state. Disk durability is an asynchronous side effect and cannot
          // stall input, runtime behavior, or the frame submitted below.
          apply_settings(next);
          settings_changed = true;
          sound_.ui_select();
        } else {
          save_notice_success_ = false;
          save_notice_until_ = now + std::chrono::seconds(8);
          save_notice_changed = true;
        }
      }
    }
    save_notice_changed |= consume_save_results();
    if (dashboard_events.hover_changed || dashboard_events.tab_changed || settings_changed ||
        save_notice_changed) {
      update_dashboard(latest_resolution_, last_frame);
      last_dashboard_refresh_at = std::chrono::steady_clock::now();
    }
    if ((dashboard_events.hover_changed || dashboard_events.tab_changed || settings_changed) &&
        dashboard_events.max_event_age_seconds > 0.0F) {
      const auto interaction_finished = std::chrono::steady_clock::now();
      const auto local_ms =
          std::chrono::duration<double, std::milli>(interaction_finished - input_finished).count();
      const auto end_to_end_ms = dashboard_events.max_event_age_seconds * 1000.0 + local_ms;
      if (end_to_end_ms >= 16.0 &&
          interaction_finished - last_slow_interaction_at >= std::chrono::seconds(5)) {
        core::DiagnosticRecord record{core::DiagnosticEventType::Error,
                                      "DASHBOARD_INTERACTION_SLOW"};
        record.details = {
            {"event_queue_ms", std::to_string(dashboard_events.max_event_age_seconds * 1000.0F)},
            {"logic_and_submit_ms", std::to_string(local_ms)},
            {"end_to_end_ms", std::to_string(end_to_end_ms)}};
        (void)log_.append(record);
        last_slow_interaction_at = interaction_finished;
      }
    }
    if (thumbnail_dirty_) {
      thumbnail_dirty_ = false;
      if (!overlays_->update_thumbnail(
              text(core::StringId::ThumbnailTitle), text(core::StringId::ThumbnailDescription),
              std::string(core::localize(core::StringId::AppName, language_))))
        log_overlay_error(std::chrono::steady_clock::now());
    }
    const auto tracking_started = std::chrono::steady_clock::now();
    const auto frame = vr_.sample_tracking();
    const auto tracking_finished = std::chrono::steady_clock::now();
    if (tracking_finished - tracking_started >= std::chrono::milliseconds(32) &&
        tracking_finished - last_slow_tracking_at >= std::chrono::seconds(5)) {
      core::DiagnosticRecord record{core::DiagnosticEventType::Error, "TRACKING_SAMPLE_SLOW"};
      record.status = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         tracking_finished - tracking_started)
                                         .count()) +
                      "ms";
      (void)log_.append(record);
      last_slow_tracking_at = tracking_finished;
    }
    if (now - last_tracking_log_at_ >= std::chrono::seconds(1)) {
      core::DiagnosticRecord record{};
      record.event = core::DiagnosticEventType::AvailabilityChanged;
      record.code = "TRACKING_SAMPLE";
      record.debug_only = true;
      record.details = {
          {"source", "GetDeviceToAbsoluteTrackingPose"},
          {"universe", "standing"},
          {"prediction_seconds", "0"},
          {"sample_interval_ms",
           last_frame.sampled_at == std::chrono::steady_clock::time_point{}
               ? "first"
               : std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                    frame.sampled_at - last_frame.sampled_at)
                                    .count())},
          {"sample_age_ms", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::steady_clock::now() - frame.sampled_at)
                                               .count())},
          {"hmd_connected", frame.headset_connected ? "true" : "false"},
          {"hmd_tracked", frame.headset_tracked ? "true" : "false"},
          {"hmd_pose_valid", frame.headset_pose_valid ? "true" : "false"},
          {"hmd_tracking_result", tracking_result_name(frame.headset_tracking_result)},
          {"left_device_index", std::to_string(frame.left.device_index)},
          {"left_connected", frame.left.connected ? "true" : "false"},
          {"left_tracked", frame.left.tracked ? "true" : "false"},
          {"left_pose_valid", frame.left.pose_valid ? "true" : "false"},
          {"left_tracking_result", tracking_result_name(frame.left.tracking_result)},
          {"right_device_index", std::to_string(frame.right.device_index)},
          {"right_connected", frame.right.connected ? "true" : "false"},
          {"right_tracked", frame.right.tracked ? "true" : "false"},
          {"right_pose_valid", frame.right.pose_valid ? "true" : "false"},
          {"right_tracking_result", tracking_result_name(frame.right.tracking_result)},
      };
      (void)log_.append(record);
      last_tracking_log_at_ = now;
    }
    last_frame = frame;
    if (!frame.headset_connected || (!frame.left.connected && !frame.right.connected)) {
      if (!no_devices_since)
        no_devices_since = now;
      else if (flow_->state() != core::FlowState::Exiting &&
               now - *no_devices_since >= std::chrono::seconds(30)) {
        (void)log_.append({core::DiagnosticEventType::Lifecycle, "VR_DEVICES_UNAVAILABLE"});
        break;
      }
    } else {
      no_devices_since.reset();
    }
    const bool dashboard_visible = dashboard_open();
    const auto prereq = prerequisites(frame, latest_resolution_, dashboard_visible);
    std::string availability =
        "game=" + std::string(platform::windows::diagnostic_code(latest_resolution_.game_status)) +
        ";target=" + platform::windows::diagnostic_code(latest_resolution_.target_status) +
        ";hardware=" + (prereq.hardware_available ? "1" : "0") +
        ";hmd=" + (prereq.headset_tracked ? "1" : "0") +
        ";hand=" + (prereq.configured_hand_tracked ? "1" : "0") +
        ";pointer=" + (prereq.opposite_pointer_available ? "1" : "0") +
        ";dashboard=" + (dashboard_visible ? "1" : "0");
    if (availability != availability_signature_) {
      availability_signature_ = availability;
      core::DiagnosticRecord r{};
      r.event = core::DiagnosticEventType::AvailabilityChanged;
      r.code = "RUNTIME_PREREQUISITES_CHANGED";
      r.status = availability;
      r.current_game_status = platform::windows::diagnostic_code(latest_resolution_.game_status);
      r.exit_target_status = platform::windows::diagnostic_code(latest_resolution_.target_status);
      r.details = {{"flow_state", std::string(core::to_string(flow_->state()))},
                   {"last_exit", std::string(core::to_string(flow_->snapshot(now).last_exit))},
                   {"configured_hand", std::string(core::to_string(settings_.configured_hand))},
                   {"ready", prereq.ready() ? "true" : "false"},
                   {"scene_pid", std::to_string(observed_scene_pid)}};
      (void)log_.append(r);
    }
    apply(flow_->on_prerequisites(now, prereq), now, frame);
    if (pending_confirmation) {
      const bool timed_out =
          std::chrono::steady_clock::now() - confirmation_requested_at >= kMaxTargetResolutionAge;
      if (confirmation_resolution || timed_out || dashboard_visible ||
          flow_->state() != core::FlowState::Confirming) {
        const bool same_target =
            confirmation_resolution && confirmation_resolution->target &&
            confirmation_resolution->scene_pid == observed_scene_pid &&
            core_target(*confirmation_resolution->target) == core_target(*pending_confirmation);
        const bool accepted = !timed_out && !dashboard_visible && same_target &&
                              flow_->state() == core::FlowState::Confirming;
        if (accepted) {
          apply(flow_->on_prerequisites(now, prerequisites(frame, *confirmation_resolution)), now,
                frame);
          if (flow_->state() == core::FlowState::Confirming) {
            locked_native_ = confirmation_resolution->target;
            apply(flow_->on_confirm(now, core_target(*locked_native_),
                                    std::chrono::seconds(settings_.countdown_seconds)),
                  now, frame);
          }
        }
        if (flow_->state() == core::FlowState::Confirming) {
          locked_native_.reset();
          core::DiagnosticRecord blocked{};
          blocked.event = timed_out ? core::DiagnosticEventType::Error
                                    : core::DiagnosticEventType::StateTransition;
          blocked.code = timed_out ? "CONFIRM_RESOLUTION_TIMEOUT" : "CONFIRM_BLOCKED";
          blocked.debug_only = !timed_out;
          blocked.details = {{"dashboard_visible", dashboard_visible ? "true" : "false"},
                             {"same_target", same_target ? "true" : "false"}};
          (void)log_.append(blocked);
          apply(flow_->on_dismiss(), now, frame);
        }
        pending_confirmation.reset();
      }
    }
    const auto& configured =
        settings_.configured_hand == core::Hand::Left ? frame.left : frame.right;
    const auto& opposite = settings_.configured_hand == core::Hand::Left ? frame.right : frame.left;
    bool gesture = false;
    if (dashboard_visible) {
      gesture_.reset();
    } else
      gesture = gesture_.update({now, configured.tracked && frame.headset_tracked,
                                 cv(configured.position), cv(frame.headset_position),
                                 cv(frame.headset_forward), configured.input_active});
    const auto& gesture_diagnostic = gesture_.diagnostic();
    const std::string gesture_decision =
        dashboard_visible ? "dashboard_open"
                          : std::string(core::to_string(gesture_diagnostic.decision));
    if ((gesture_decision != last_gesture_decision_ &&
         now - last_gesture_log_at_ >= std::chrono::milliseconds(250)) ||
        now - last_gesture_log_at_ >= std::chrono::seconds(2)) {
      core::DiagnosticRecord record{};
      record.event = core::DiagnosticEventType::GestureRecognized;
      record.code = "GESTURE_EVALUATED";
      record.debug_only = true;
      record.status = gesture_decision;
      record.details = {
          {"configured_hand", std::string(core::to_string(settings_.configured_hand))},
          {"device_index", std::to_string(configured.device_index)},
          {"tracked", configured.tracked ? "true" : "false"},
          {"headset_tracked", frame.headset_tracked ? "true" : "false"},
          {"sample_count", std::to_string(gesture_diagnostic.sample_count)},
          {"window_span_ms", std::to_string(gesture_diagnostic.span_ms)},
          {"head_distance_m", std::to_string(gesture_diagnostic.head_distance_m)},
          {"behind_head_m", std::to_string(gesture_diagnostic.behind_head_m)},
          {"amplitude_m", std::to_string(gesture_diagnostic.amplitude_m)},
          {"lateral_travel_m", std::to_string(gesture_diagnostic.lateral_travel_m)},
          {"maximum_orthogonal_ratio", std::to_string(gesture_diagnostic.maximum_orthogonal_ratio)},
          {"minimum_stroke_efficiency",
           std::to_string(gesture_diagnostic.minimum_stroke_efficiency)},
          {"stroke_length_ratio", std::to_string(gesture_diagnostic.stroke_length_ratio)},
          {"stroke_timing_ratio", std::to_string(gesture_diagnostic.stroke_timing_ratio)},
          {"endpoint_drift_m", std::to_string(gesture_diagnostic.endpoint_drift_m)},
          {"maximum_speed_mps", std::to_string(gesture_diagnostic.maximum_speed_mps)},
          {"confidence", std::to_string(gesture_diagnostic.confidence)},
          {"input_active", gesture_diagnostic.input_active ? "true" : "false"},
          {"reversals", std::to_string(gesture_diagnostic.reversals)},
          {"sensitivity", core::to_string(settings_.gesture_sensitivity)},
          {"required_reversals", std::to_string(gesture_.config().required_reversals)},
          {"minimum_stroke_m", std::to_string(gesture_.config().minimum_stroke_metres)},
          {"minimum_total_lateral_travel_m",
           std::to_string(gesture_.config().minimum_total_lateral_travel_metres)},
          {"minimum_gesture_duration_ms",
           std::to_string(gesture_.config().minimum_gesture_duration.count())},
          {"minimum_stroke_duration_ms",
           std::to_string(gesture_.config().minimum_stroke_duration.count())},
          {"maximum_stroke_duration_ms",
           std::to_string(gesture_.config().maximum_stroke_duration.count())},
          {"minimum_head_distance_m",
           std::to_string(gesture_.config().minimum_head_distance_metres)},
          {"maximum_head_distance_m",
           std::to_string(gesture_.config().maximum_head_distance_metres)},
          {"maximum_behind_head_m", std::to_string(gesture_.config().maximum_behind_head_metres)},
          {"clock_reset", gesture_diagnostic.clock_reset ? "true" : "false"},
          {"prerequisites_ready", prereq.ready() ? "true" : "false"},
      };
      (void)log_.append(record);
      last_gesture_decision_ = gesture_decision;
      last_gesture_log_at_ = now;
    }
    if (gesture && !dashboard_open()) {
      core::DiagnosticRecord recognized{};
      recognized.event = core::DiagnosticEventType::GestureRecognized;
      recognized.code = "GESTURE_RECOGNIZED";
      recognized.status = prereq.ready() ? "READY" : availability;
      recognized.current_game_status =
          platform::windows::diagnostic_code(latest_resolution_.game_status);
      recognized.exit_target_status =
          platform::windows::diagnostic_code(latest_resolution_.target_status);
      recognized.details = {
          {"flow_state", std::string(core::to_string(flow_->state()))},
          {"configured_hand", std::string(core::to_string(settings_.configured_hand))},
          {"device_index", std::to_string(configured.device_index)},
          {"prerequisites_ready", prereq.ready() ? "true" : "false"}};
      (void)log_.append(recognized);
      auto effects = flow_->on_gesture(now, prereq);
      core::DiagnosticRecord dispatch{};
      dispatch.event = core::DiagnosticEventType::StateTransition;
      dispatch.code = "GESTURE_DISPATCH";
      dispatch.debug_only = true;
      dispatch.details = {{"effect_count", std::to_string(effects.size())},
                          {"state_after", std::string(core::to_string(flow_->state()))},
                          {"ready", prereq.ready() ? "true" : "false"}};
      (void)log_.append(dispatch);
      apply(std::move(effects), now, frame);
    } else if (gesture) {
      core::DiagnosticRecord record{};
      record.event = core::DiagnosticEventType::GestureRecognized;
      record.code = "GESTURE_SUPPRESSED_DASHBOARD";
      record.debug_only = true;
      (void)log_.append(record);
    }
    const auto state = flow_->state();
    const auto wrist_events = overlays_->poll_wrist_events(
        opposite, frame.headset_position, !dashboard_visible,
        state == core::FlowState::Confirming, state == core::FlowState::CountingDown);
    if (!overlays_->error().empty())
      abort_overlay_flow(now, frame);
    else if (!dashboard_visible) {
      if (wrist_events.panel_hover_changed)
        apply(flow_->on_control_hover(now, wrist_events.panel_hovered), now, frame);
      if (wrist_events.entered_button &&
          now - last_ui_hover_sound_at >= std::chrono::milliseconds(150)) {
        sound_.ui_hover();
        last_ui_hover_sound_at = now;
      }
      if (wrist_events.clicked_button)
        sound_.ui_select();
      for (const auto action : wrist_events.actions) {
        if (dashboard_open())
          break;
        core::DiagnosticRecord action_record{};
        action_record.event = core::DiagnosticEventType::StateTransition;
        action_record.code = "WRIST_ACTION";
        action_record.debug_only = true;
        action_record.status = wrist_action_name(action);
        action_record.details = {{"flow_state", std::string(core::to_string(flow_->state()))},
                                 {"pointer_device_index", std::to_string(opposite.device_index)},
                                 {"pointer_tracked", opposite.tracked ? "true" : "false"}};
        (void)log_.append(action_record);
        if (action == platform::openvr::OverlayAction::Primary)
          apply(flow_->on_control_clicked(now), now, frame);
        else if (action == platform::openvr::OverlayAction::Dismiss) {
          pending_confirmation.reset();
          apply(flow_->on_dismiss(), now, frame);
        } else if (action == platform::openvr::OverlayAction::Cancel)
          apply(flow_->on_cancel(), now, frame);
        else if (action == platform::openvr::OverlayAction::Confirm) {
          if (!pending_confirmation && !dashboard_open() &&
              flow_->state() == core::FlowState::Confirming && latest_resolution_.target &&
              latest_resolution_at != std::chrono::steady_clock::time_point{} &&
              std::chrono::steady_clock::now() - latest_resolution_at < kMaxTargetResolutionAge) {
            pending_confirmation = latest_resolution_.target;
            confirmation_requested_at = std::chrono::steady_clock::now();
            target_worker.request_refresh();
          } else if (!pending_confirmation) {
            core::DiagnosticRecord blocked{};
            blocked.event = core::DiagnosticEventType::StateTransition;
            blocked.code = "CONFIRM_BLOCKED";
            blocked.debug_only = true;
            blocked.details = {{"dashboard_visible", dashboard_open() ? "true" : "false"},
                               {"flow_state", std::string(core::to_string(flow_->state()))}};
            (void)log_.append(blocked);
            apply(flow_->on_dismiss(), now, frame);
          }
        }
      }
    }
    const auto process_status = process_.poll(now);
    if (flow_->state() == core::FlowState::Exiting &&
        (process_status == platform::windows::CloseStatus::ExitedGracefully ||
         process_status == platform::windows::CloseStatus::ForceSucceeded ||
         process_status == platform::windows::CloseStatus::ForceTimedOut ||
         process_status == platform::windows::CloseStatus::Failed ||
         process_status == platform::windows::CloseStatus::IdentityInvalid)) {
      if (process_status == platform::windows::CloseStatus::ForceSucceeded)
        apply(flow_->on_force_result(now, core::CloseRequestResult::TargetExited), now, frame);
      else if (process_status == platform::windows::CloseStatus::ForceTimedOut)
        apply(flow_->on_force_result(now, core::CloseRequestResult::Failed,
                                     "EXIT_FORCE_WAIT_TIMEOUT"),
              now, frame);
      else if (process_status == platform::windows::CloseStatus::Failed)
        apply(flow_->on_force_result(now, core::CloseRequestResult::Failed,
                                     "EXIT_PROCESS_WAIT_FAILED"),
              now, frame);
      else
        apply(flow_->on_target_status(
                  now, process_status == platform::windows::CloseStatus::ExitedGracefully
                           ? core::TargetStatus::Exited
                           : core::TargetStatus::IdentityMismatch),
              now, frame);
    }
    apply(flow_->tick(now), now, frame);
    // Preserve the start-of-iteration sample so a visibility change that
    // arrives later in this iteration is handled immediately next time.
    dashboard_was_open = dashboard_at_start;
    const auto dashboard_refresh_interval =
        dashboard_visible ? std::chrono::milliseconds(50) : std::chrono::milliseconds(250);
    if (std::chrono::steady_clock::now() - last_dashboard_refresh_at >=
        dashboard_refresh_interval) {
      update_dashboard(latest_resolution_, frame);
      last_dashboard_refresh_at = std::chrono::steady_clock::now();
    }
    const auto loop_finished = std::chrono::steady_clock::now();
    const auto loop_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(loop_finished - loop_start);
    if (loop_duration >= std::chrono::milliseconds(250) &&
        loop_finished - last_slow_loop_at >= std::chrono::seconds(30)) {
      core::DiagnosticRecord record{};
      record.event = core::DiagnosticEventType::Error;
      record.code = "RUNTIME_LOOP_SLOW";
      record.status = std::to_string(loop_duration.count()) + "ms";
      (void)log_.append(record);
      last_slow_loop_at = loop_finished;
    }
    timer_resolution.set_active(dashboard_visible);
    (void)WaitForSingleObject(stop_event.get(), dashboard_visible ? 4 : 11);
  }
  target_worker.request_stop();
  settings_writer.stop();
  (void)consume_save_results();
  target_worker.stop();
  log_.stop_async();
  process_.clear();
  overlays_->shutdown();
  vr_.shutdown();
  (void)log_.append({core::DiagnosticEventType::Lifecycle, "RUNTIME_STOPPED"});
  return 0;
}
}  // namespace bailout::app
