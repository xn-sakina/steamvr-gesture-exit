#include "app/runtime_app.hpp"
#include "platform/windows/paths.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <thread>

namespace bailout::app {
namespace {
constexpr char kAppKey[] = "com.steamvr-bail-out.overlay";
const char* wrist_action_name(platform::openvr::OverlayAction action) {
  switch (action) {
    case platform::openvr::OverlayAction::Primary: return "primary";
    case platform::openvr::OverlayAction::Confirm: return "confirm";
    case platform::openvr::OverlayAction::Dismiss: return "dismiss";
    case platform::openvr::OverlayAction::Cancel: return "cancel";
  }
  return "unknown";
}
const char* tracking_result_name(int value) {
  switch (static_cast<vr::ETrackingResult>(value)) {
    case vr::TrackingResult_Uninitialized: return "uninitialized";
    case vr::TrackingResult_Calibrating_InProgress: return "calibrating";
    case vr::TrackingResult_Calibrating_OutOfRange: return "calibrating_out_of_range";
    case vr::TrackingResult_Running_OK: return "running_ok";
    case vr::TrackingResult_Running_OutOfRange: return "running_out_of_range";
    case vr::TrackingResult_Fallback_RotationOnly: return "rotation_only";
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
  HANDLE get() const { return value_; }

 private:
  HANDLE value_;
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

class SteamVrServerMonitor {
 public:
  bool attach() {
    std::array<char, 4096> runtime_path{};
    uint32_t required = 0;
    if (!vr::VR_GetRuntimePath(runtime_path.data(), static_cast<uint32_t>(runtime_path.size()),
                               &required))
      return false;
    const auto expected =
        (std::filesystem::path(widen(runtime_path.data())) / L"bin" / L"win64" /
         L"vrserver.exe")
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
      HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE,
                                   entry.th32ProcessID);
      if (!process)
        continue;
      std::array<wchar_t, 32768> image{};
      DWORD length = static_cast<DWORD>(image.size());
      const bool matches =
          QueryFullProcessImageNameW(process, 0, image.data(), &length) &&
          _wcsicmp(std::filesystem::path(std::wstring(image.data(), length))
                        .lexically_normal()
                        .c_str(),
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
  bool alive() const { return process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT; }

 private:
  HANDLE process_ = nullptr;
};

// Process inspection and application metadata requests may wait on SteamVR or
// Windows. Keep them off the thread that polls overlay input and submits UI.
class ForegroundResolutionWorker {
 public:
  ForegroundResolutionWorker(const platform::windows::ExitTargetResolver& resolver,
                             vr::IVRApplications* applications, core::DiagnosticLog& log)
      : thread_([this, &resolver, applications, &log] {
          auto last_slow_log = std::chrono::steady_clock::time_point{};
          auto next_autolaunch_check = std::chrono::steady_clock::time_point{};
          while (!stop_) {
            const auto started = std::chrono::steady_clock::now();
            if (started >= next_autolaunch_check) {
              autolaunch_disabled_ = !applications->IsApplicationInstalled(kAppKey) ||
                                     !applications->GetApplicationAutoLaunch(kAppKey);
              next_autolaunch_check = started + std::chrono::seconds(1);
            }
            auto resolution = resolver.resolve_foreground();
            const auto finished = std::chrono::steady_clock::now();
            const auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(finished - started);
            {
              std::scoped_lock lock(mutex_);
              latest_ = std::move(resolution);
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
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(250),
                           [this] { return stop_.load() || refresh_requested_; });
            refresh_requested_ = false;
          }
        }) {}
  ~ForegroundResolutionWorker() { stop(); }
  ForegroundResolutionWorker(const ForegroundResolutionWorker&) = delete;
  ForegroundResolutionWorker& operator=(const ForegroundResolutionWorker&) = delete;
  void stop() {
    stop_ = true;
    wake_.notify_all();
    if (thread_.joinable())
      thread_.join();
  }
  bool take(platform::windows::TargetResolution& resolution,
            std::chrono::steady_clock::time_point& completed_at) {
    std::scoped_lock lock(mutex_);
    if (!pending_)
      return false;
    resolution = std::move(latest_);
    completed_at = latest_at_;
    pending_ = false;
    return true;
  }
  bool autolaunch_disabled() const { return autolaunch_disabled_.load(); }
  void request_refresh() {
    {
      std::scoped_lock lock(mutex_);
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
  std::chrono::steady_clock::time_point latest_at_{};
  bool pending_ = false;
  bool refresh_requested_ = false;
  std::thread thread_;
};

class SettingsPersistenceWorker {
 public:
  SettingsPersistenceWorker(const std::filesystem::path& path, core::DiagnosticLog& log)
      : store_(path), log_(log), thread_([this] { run(); }) {}
  ~SettingsPersistenceWorker() { stop(); }
  SettingsPersistenceWorker(const SettingsPersistenceWorker&) = delete;
  SettingsPersistenceWorker& operator=(const SettingsPersistenceWorker&) = delete;
  void enqueue(SettingsChange change) {
    {
      std::scoped_lock lock(mutex_);
      pending_.push_back(std::move(change));
    }
    wake_.notify_one();
  }
  bool take_failure() { return failures_.exchange(0) != 0; }
  void stop() {
    {
      std::scoped_lock lock(mutex_);
      stop_ = true;
    }
    wake_.notify_all();
    if (thread_.joinable())
      thread_.join();
  }

 private:
  void run() {
    while (true) {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [this] { return stop_ || !pending_.empty(); });
      if (pending_.empty())
        return;
      auto change = std::move(pending_.front());
      pending_.pop_front();
      lock.unlock();
      std::string error;
      const bool saved = store_.save(change.settings, &error);
      core::DiagnosticRecord record{};
      record.event = saved ? core::DiagnosticEventType::SettingsChanged
                           : core::DiagnosticEventType::Error;
      record.code = saved ? "SETTINGS_SAVED" : error;
      if (saved) {
        record.setting_name = std::move(change.name);
        record.setting_value = std::move(change.value);
      } else {
        failures_.fetch_add(1);
      }
      (void)log_.append(record);
    }
  }
  core::SettingsStore store_;
  core::DiagnosticLog& log_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<SettingsChange> pending_;
  bool stop_ = false;
  std::atomic_uint failures_ = 0;
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
    recovery_notice_pending_ = true;
  }
  flow_ = std::make_unique<core::FlowStateMachine>(
      std::chrono::seconds(settings_.control_timeout_seconds));
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
std::vector<SettingsChange> RuntimeApp::dashboard_input(
    const std::vector<core::DashboardAction>& actions) {
  std::vector<SettingsChange> changes;
  for (const auto action : actions) {
    auto candidate = settings_;
    if (!core::apply_dashboard_action(candidate, action))
      continue;
    sound_.ui_select();
    const char* name = nullptr;
    std::string value;
    if (candidate.configured_hand != settings_.configured_hand) {
      name = "configured_hand";
      value = core::to_string(candidate.configured_hand);
    } else if (candidate.countdown_seconds != settings_.countdown_seconds) {
      name = "countdown_seconds";
      value = std::to_string(candidate.countdown_seconds);
    } else if (candidate.control_timeout_seconds != settings_.control_timeout_seconds) {
      name = "control_timeout_seconds";
      value = std::to_string(candidate.control_timeout_seconds);
    } else if (candidate.countdown_sound != settings_.countdown_sound) {
      name = "countdown_sound";
      value = candidate.countdown_sound ? "true" : "false";
    } else if (candidate.language != settings_.language) {
      name = "language";
      value = core::to_string(candidate.language);
    }
    if (!name)
      continue;
    const bool hand_changed = candidate.configured_hand != settings_.configured_hand;
    const bool timeout_changed =
        candidate.control_timeout_seconds != settings_.control_timeout_seconds;
    const bool language_changed = candidate.language != settings_.language;
    settings_ = candidate;
    if (hand_changed)
      gesture_.reset();
    if (timeout_changed)
      flow_->set_control_timeout(std::chrono::seconds(settings_.control_timeout_seconds));
    if (language_changed) {
      language_ = core::resolve_language(settings_.language, steam_language_, os_language_);
      thumbnail_dirty_ = true;
    }
    changes.push_back({candidate, name, std::move(value)});
  }
  return changes;
}
void RuntimeApp::update_dashboard(const platform::windows::TargetResolution& resolution,
                                  const TrackingFrame& f) {
  if (!overlays_)
    return;
  const auto& configured = settings_.configured_hand == core::Hand::Left ? f.left : f.right;
  const auto& opposite = settings_.configured_hand == core::Hand::Left ? f.right : f.left;
  const auto now = std::chrono::steady_clock::now();
  if (recovery_notice_pending_ && overlays_->dashboard_visible()) {
    recovery_notice_pending_ = false;
    recovery_notice_until_ = now + std::chrono::seconds(8);
  }
  const auto snapshot = flow_->snapshot(now);
  platform::openvr::DashboardSurfaceModel model{};
  model.title = text(core::StringId::AppName);
  model.settings_title = text(core::StringId::SettingsTitle);
  model.diagnostics_title = text(core::StringId::DiagnosticsTitle);
  model.help_title = text(core::StringId::QuickGuide);
  if (now < save_failure_notice_until_)
    model.notice = text(core::StringId::SettingsSaveFailed);
  else if (recovery_notice_pending_ || now < recovery_notice_until_)
    model.notice = text(core::StringId::SettingsRecovered);
  model.settings = {
      {core::DashboardSettingRow::ConfiguredHand, text(core::StringId::ConfiguredHand), L""},
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
      game_available ? resolution.game->display_name : text(core::StringId::Unavailable);
  const std::wstring game_detail =
      game_available ? text(core::StringId::Ready) : text(game_reason(resolution.game_status));
  const std::wstring target_value =
      target_available ? resolution.target->display_name : text(core::StringId::Unavailable);
  const std::wstring target_detail =
      target_available ? text(core::StringId::Ready) : text(target_reason(resolution.target_status));
  const std::wstring last = widen(core::localize_last_exit(snapshot.last_exit, language_));
  model.statuses = {
      {text(core::StringId::CurrentVrGame), game_value, game_detail,
       game_available ? platform::openvr::DashboardStatusTone::Available
                      : platform::openvr::DashboardStatusTone::Unavailable},
      {text(core::StringId::ExitTarget), target_value, target_detail,
       target_available ? platform::openvr::DashboardStatusTone::Available
                        : platform::openvr::DashboardStatusTone::Unavailable},
      {text(core::StringId::HandTracking),
       configured.tracked ? text(core::StringId::ControllerTracked)
                          : text(core::StringId::ControllerNotTracked),
       text(configured.tracked ? core::StringId::WaveHandReady
            : configured.connected ? core::StringId::WaveHandTrackingLost
                                   : core::StringId::WaveHandDisconnected),
       configured.tracked ? platform::openvr::DashboardStatusTone::Available
                          : platform::openvr::DashboardStatusTone::Unavailable},
      {text(core::StringId::OppositePointer),
       opposite.pointer_available() ? text(core::StringId::ControllerTracked)
                                    : text(core::StringId::ControllerNotTracked),
       text(opposite.pointer_available() ? core::StringId::ClickHandReady
            : opposite.connected ? core::StringId::ClickHandTrackingLost
                                 : core::StringId::ClickHandDisconnected),
       opposite.pointer_available() ? platform::openvr::DashboardStatusTone::Available
                                    : platform::openvr::DashboardStatusTone::Unavailable},
      {text(core::StringId::LastExit), last, L"", platform::openvr::DashboardStatusTone::Neutral}};
  model.help_lines = {text(core::StringId::HelpGesture), text(core::StringId::HelpOppositeHand),
                      text(core::StringId::HelpForceClose)};
  const auto submit_started = std::chrono::steady_clock::now();
  const bool submitted = overlays_->update_dashboard(model);
  const auto submit_finished = std::chrono::steady_clock::now();
  const auto submit_duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(submit_finished - submit_started);
  if (submit_duration >= std::chrono::milliseconds(250) &&
      submit_finished - last_slow_dashboard_at_ >= std::chrono::seconds(30)) {
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
    const auto& hand = settings_.configured_hand == core::Hand::Left ? f.left : f.right;
    switch (e.type) {
      case core::EffectType::ShowIdleControl:
        if (!overlays_->show_wrist(
                hand, f.sampled_at,
                {text(core::StringId::AppName), text(core::StringId::ExitGame),
                 text(core::StringId::ExitGame), text(core::StringId::Dismiss)})) {
          abort_overlay_flow(now, f);
          return;
        }
        break;
      case core::EffectType::ShowConfirmation: {
        std::wstring name = latest_resolution_.target ? latest_resolution_.target->display_name
                                                      : text(core::StringId::UnknownGame);
        if (!overlays_->show_wrist(
                hand, f.sampled_at,
                {text(core::StringId::ConfirmTitle),
                 name + L"\n" + text(core::StringId::ConfirmBody), text(core::StringId::Confirm),
                 text(core::StringId::Dismiss)})) {
          abort_overlay_flow(now, f);
          return;
        }
        break;
      }
      case core::EffectType::HideControl:
        if (!overlays_->hide_wrist())
          log_overlay_error(now);
        break;
      case core::EffectType::ShowCountdownHud:
      case core::EffectType::UpdateCountdownHud: {
        double seconds = std::max(0.0, e.remaining.count() / 1000.0);
        std::wstringstream value;
        value << static_cast<int>(std::ceil(seconds)) << text(core::StringId::SecondsUnit);
        const std::wstring target_name =
            e.target ? widen(e.target->display_name) : text(core::StringId::UnknownGame);
        const std::wstring countdown_body = target_name + L"\n" + value.str();
        if (!overlays_->show_hud({text(core::StringId::ExitGame), countdown_body, L"", L"", L"",
                                  static_cast<float>(e.progress)})) {
          abort_overlay_flow(now, f);
          return;
        }
        if (hand.tracked && !overlays_->show_wrist(hand, f.sampled_at,
                                                   {text(core::StringId::ExitGame), countdown_body,
                                                    L"", text(core::StringId::CancelExit)})) {
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
      case core::EffectType::ShowExitingHud:
        if (!overlays_->hide_wrist()) {
          abort_overlay_flow(now, f);
          return;
        }
        if (!overlays_->show_hud(
                {text(core::StringId::Exiting), text(core::StringId::PleaseWait)})) {
          abort_overlay_flow(now, f);
          return;
        }
        break;
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
        if (!locked_native_ || !targets_->revalidate(*locked_native_)) {
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
        if (!locked_native_ || !targets_->revalidate(*locked_native_)) {
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
    vr_.shutdown();
    return 0;
  }
  if (!server.alive()) {
    (void)log_.append({core::DiagnosticEventType::Lifecycle, "STEAMVR_SERVER_EXITED"});
    vr_.shutdown();
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
    vr_.shutdown();
    return 1;
  }
  const auto dashboard_open = [this] {
    return vr_.overlays()->IsDashboardVisible() || overlays_->dashboard_visible();
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
  SettingsPersistenceWorker settings_writer(settings_store_.path(), log_);
  ForegroundResolutionWorker target_worker(*targets_, vr_.applications(), log_);
  const auto started_at = std::chrono::steady_clock::now();
  auto next_server_check = started_at;
  std::optional<std::chrono::steady_clock::time_point> no_devices_since;
  TrackingFrame last_frame{};
  bool dashboard_was_open = dashboard_open();
  std::uint32_t observed_scene_pid = 0;
  auto observed_scene_state = vr::EVRSceneApplicationState_None;
  auto last_ui_hover_sound_at = std::chrono::steady_clock::time_point{};
  auto last_slow_loop_at = std::chrono::steady_clock::time_point{};
  auto last_target_log_at = std::chrono::steady_clock::time_point{};
  std::string last_target_signature;
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
      record.details["steamvr_global_visible"] = vr_.overlays()->IsDashboardVisible() ? "true" : "false";
      record.details["overlay_visible"] = overlays_->dashboard_visible() ? "true" : "false";
      (void)log_.append(record);
      target_worker.request_refresh();
      update_dashboard(latest_resolution_, last_frame);
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
    while (vr_.system()->PollNextEvent(&system_event, sizeof(system_event))) {
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
        case vr::VREvent_ApplicationListUpdated:
          scene_event = true;
          break;
        default:
          break;
      }
    }
    if (vr_quit)
      break;
    const auto now = std::chrono::steady_clock::now();
    const auto scene_state = vr_.applications()->GetSceneApplicationState();
    const bool scene_active = scene_state == vr::EVRSceneApplicationState_Running ||
                              scene_state == vr::EVRSceneApplicationState_Waiting;
    const auto scene_pid =
        scene_active ? vr_.applications()->GetCurrentSceneProcessId() : 0;
    if (scene_state != observed_scene_state || scene_pid != observed_scene_pid) {
      core::DiagnosticRecord scene_record{};
      scene_record.event = core::DiagnosticEventType::AvailabilityChanged;
      scene_record.code = "SCENE_CHANGED";
      scene_record.debug_only = true;
      scene_record.details = {{"previous_scene_pid", std::to_string(observed_scene_pid)},
                              {"scene_pid", std::to_string(scene_pid)},
                              {"scene_state", std::to_string(static_cast<int>(scene_state))}};
      (void)log_.append(scene_record);
      observed_scene_state = scene_state;
      observed_scene_pid = scene_pid;
      if (latest_resolution_.scene_pid != scene_pid) {
        latest_resolution_ = {};
        latest_resolution_.game_status = scene_pid
                                             ? platform::windows::CurrentVrGameStatus::Resolving
                                             : platform::windows::CurrentVrGameStatus::SceneNotRunning;
      }
      target_worker.request_refresh();
    } else if (scene_event) {
      target_worker.request_refresh();
    }
    platform::windows::TargetResolution resolved{};
    std::chrono::steady_clock::time_point resolved_at{};
    if (target_worker.take(resolved, resolved_at)) {
      const bool current_scene = resolved.scene_pid == observed_scene_pid;
      const std::string signature = std::to_string(resolved.scene_pid) + ":" +
          platform::windows::diagnostic_code(resolved.game_status) + ":" +
          platform::windows::diagnostic_code(resolved.target_status) + ":" +
          (current_scene ? "current" : "stale");
      if (signature != last_target_signature || now - last_target_log_at >= std::chrono::seconds(5)) {
        core::DiagnosticRecord record{};
        record.event = core::DiagnosticEventType::AvailabilityChanged;
        record.code = "TARGET_RESOLUTION_RECEIVED";
        record.debug_only = true;
        record.current_game_status = platform::windows::diagnostic_code(resolved.game_status);
        record.exit_target_status = platform::windows::diagnostic_code(resolved.target_status);
        record.details = {{"resolved_scene_pid", std::to_string(resolved.scene_pid)},
                          {"observed_scene_pid", std::to_string(observed_scene_pid)},
                          {"scene_matches", current_scene ? "true" : "false"},
                          {"result_age_ms", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - resolved_at).count())},
                          {"has_game", resolved.game ? "true" : "false"},
                          {"has_target", resolved.target ? "true" : "false"}};
        (void)log_.append(record);
        last_target_signature = signature;
        last_target_log_at = now;
      }
      if (current_scene)
        latest_resolution_ = std::move(resolved);
    }
    const auto dashboard_events = overlays_->poll_dashboard_events(last_frame);
    if (!overlays_->error().empty())
      log_overlay_error(now);
    else {
      if (dashboard_events.entered_button &&
          now - last_ui_hover_sound_at >= std::chrono::milliseconds(150)) {
        sound_.ui_hover();
        last_ui_hover_sound_at = now;
      }
      for (auto& change : dashboard_input(dashboard_events.actions))
        settings_writer.enqueue(std::move(change));
    }
    if (settings_writer.take_failure())
      save_failure_notice_until_ = now + std::chrono::seconds(8);
    if (dashboard_events.hover_changed || dashboard_events.tab_changed ||
        !dashboard_events.actions.empty())
      update_dashboard(latest_resolution_, last_frame);
    if (thumbnail_dirty_) {
      thumbnail_dirty_ = false;
      if (!overlays_->update_thumbnail(
              text(core::StringId::ThumbnailTitle), text(core::StringId::ThumbnailDescription),
              std::string(core::localize(core::StringId::AppName, language_))))
        log_overlay_error(std::chrono::steady_clock::now());
    }
    const auto frame = vr_.sample_tracking();
    if (now - last_tracking_log_at_ >= std::chrono::seconds(1)) {
      core::DiagnosticRecord record{};
      record.event = core::DiagnosticEventType::AvailabilityChanged;
      record.code = "TRACKING_SAMPLE";
      record.debug_only = true;
      record.details = {
          {"source", "GetDeviceToAbsoluteTrackingPose"},
          {"universe", "standing"},
          {"prediction_seconds", "0"},
          {"sample_interval_ms", last_frame.sampled_at == std::chrono::steady_clock::time_point{}
                                     ? "first" : std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(frame.sampled_at - last_frame.sampled_at).count())},
          {"sample_age_ms", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - frame.sampled_at).count())},
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
    const auto& configured =
        settings_.configured_hand == core::Hand::Left ? frame.left : frame.right;
    const auto& opposite = settings_.configured_hand == core::Hand::Left ? frame.right : frame.left;
    bool gesture = false;
    if (dashboard_visible) {
      gesture_.reset();
      if (flow_->state() == core::FlowState::CountingDown && !overlays_->hide_wrist())
        log_overlay_error(now);
    } else
      gesture = gesture_.update({now, configured.tracked && frame.headset_tracked,
                                 cv(configured.position), cv(frame.headset_position),
                                 cv(frame.headset_forward)});
    const auto& gesture_diagnostic = gesture_.diagnostic();
    const std::string gesture_decision = dashboard_visible ? "dashboard_open" :
        std::string(core::to_string(gesture_diagnostic.decision));
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
          {"below_head_m", std::to_string(gesture_diagnostic.below_head_m)},
          {"behind_head_m", std::to_string(gesture_diagnostic.behind_head_m)},
          {"amplitude_m", std::to_string(gesture_diagnostic.amplitude_m)},
          {"reversals", std::to_string(gesture_diagnostic.reversals)},
          {"minimum_amplitude_m", std::to_string(gesture_.config().minimum_amplitude_metres)},
          {"reversal_deadband_m", std::to_string(gesture_.config().reversal_deadband_metres)},
          {"minimum_head_distance_m", std::to_string(gesture_.config().minimum_head_distance_metres)},
          {"maximum_head_distance_m", std::to_string(gesture_.config().maximum_head_distance_metres)},
          {"maximum_below_head_m", std::to_string(gesture_.config().maximum_hand_below_head_metres)},
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
      recognized.details = {{"flow_state", std::string(core::to_string(flow_->state()))},
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
    if (!dashboard_visible) {
      const auto actions = overlays_->poll_wrist_events(
          opposite, state == core::FlowState::Confirming, state == core::FlowState::CountingDown);
      if (!overlays_->error().empty())
        abort_overlay_flow(now, frame);
      else
        for (const auto action : actions) {
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
          else if (action == platform::openvr::OverlayAction::Dismiss)
            apply(flow_->on_dismiss(), now, frame);
          else if (action == platform::openvr::OverlayAction::Cancel)
            apply(flow_->on_cancel(), now, frame);
          else if (action == platform::openvr::OverlayAction::Confirm) {
            const bool dashboard_now = dashboard_open();
            platform::windows::TargetResolution fresh{};
            if (!dashboard_now)
              fresh = targets_->resolve_foreground();
            apply(flow_->on_prerequisites(now, prerequisites(frame, fresh, dashboard_now)), now,
                  frame);
            const bool same_target = fresh.target && latest_resolution_.target &&
                core_target(*fresh.target) == core_target(*latest_resolution_.target);
            if (!dashboard_now && same_target && flow_->state() == core::FlowState::Confirming) {
              locked_native_ = fresh.target;
              latest_resolution_ = fresh;
              apply(flow_->on_confirm(now, core_target(*fresh.target),
                                      std::chrono::seconds(settings_.countdown_seconds)),
                    now, frame);
            } else {
              core::DiagnosticRecord blocked{};
              blocked.event = core::DiagnosticEventType::StateTransition;
              blocked.code = "CONFIRM_BLOCKED";
              blocked.debug_only = true;
              blocked.current_game_status = platform::windows::diagnostic_code(fresh.game_status);
              blocked.exit_target_status = platform::windows::diagnostic_code(fresh.target_status);
              blocked.details = {{"dashboard_visible", dashboard_now ? "true" : "false"},
                                 {"same_target", same_target ? "true" : "false"},
                                 {"flow_state", std::string(core::to_string(flow_->state()))},
                                 {"fresh_scene_pid", std::to_string(fresh.scene_pid)},
                                 {"cached_scene_pid", std::to_string(latest_resolution_.scene_pid)}};
              (void)log_.append(blocked);
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
    if (dashboard_visible) {
      if (!overlays_->hide_wrist())
        log_overlay_error(now);
    } else if (overlays_->wrist_visible()) {
      if (!configured.tracked) {
        if (!overlays_->hide_wrist())
          log_overlay_error(now);
      } else if (!overlays_->update_wrist_pose(configured, frame.sampled_at))
        abort_overlay_flow(now, frame);
    }
    dashboard_was_open = dashboard_open();
    update_dashboard(latest_resolution_, frame);
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
    Sleep(11);
  }
  target_worker.stop();
  settings_writer.stop();
  process_.clear();
  overlays_->shutdown();
  vr_.shutdown();
  (void)log_.append({core::DiagnosticEventType::Lifecycle, "RUNTIME_STOPPED"});
  return 0;
}
}  // namespace bailout::app
