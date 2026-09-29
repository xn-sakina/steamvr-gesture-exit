#pragma once
#include "core/diagnostics.hpp"
#include "core/dashboard_ui.hpp"
#include "core/gesture_detector.hpp"
#include "core/localization.hpp"
#include "core/settings.hpp"
#include "core/settings_persistence.hpp"
#include "core/state_machine.hpp"
#include "platform/openvr/openvr_runtime.hpp"
#include "platform/openvr/overlay_manager.hpp"
#include "platform/windows/countdown_sound.hpp"
#include "platform/windows/exit_target.hpp"
#include "platform/windows/process_controller.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace steamvr_gesture_exit::app {
class RuntimeApp {
 public:
  RuntimeApp();
  ~RuntimeApp();
  int run();
  void request_stop() {
    stop_ = true;
  }

 private:
  core::TargetIdentity core_target(const RuntimeTarget&) const;
  core::Prerequisites prerequisites(const TrackingFrame&,
                                    const platform::windows::TargetResolution&,
                                    bool dashboard_visible = false) const;
  void apply(core::Effects, std::chrono::steady_clock::time_point, const TrackingFrame&);
  void update_dashboard(const platform::windows::TargetResolution&, const TrackingFrame&);
  std::optional<core::SettingsChange> dashboard_input(core::DashboardAction,
                                                       const core::Settings&);
  void apply_settings(const core::Settings&);
  void log_overlay_error(std::chrono::steady_clock::time_point);
  void abort_overlay_flow(std::chrono::steady_clock::time_point, const TrackingFrame&);
  std::wstring text(core::StringId) const;
  core::SettingsStore settings_store_;
  core::Settings settings_;
  core::Language language_ = core::Language::English;
  core::DiagnosticLog log_;
  core::GestureDetector gesture_;
  std::unique_ptr<core::FlowStateMachine> flow_;
  platform::openvr::OpenVrRuntime vr_;
  std::unique_ptr<platform::openvr::OverlayManager> overlays_;
  std::unique_ptr<platform::windows::ExitTargetResolver> targets_;
  platform::windows::ProcessController process_;
  platform::windows::CountdownSound sound_;
  platform::windows::TargetResolution latest_resolution_{};
  std::optional<RuntimeTarget> locked_native_;
  int last_beep_second_ = -1;
  std::atomic_bool stop_ = false;
  std::string steam_language_, os_language_, availability_signature_;
  std::string last_gesture_decision_;
  std::chrono::steady_clock::time_point last_gesture_log_at_{};
  std::chrono::steady_clock::time_point last_tracking_log_at_{};
  bool thumbnail_dirty_ = false;
  std::optional<bool> save_notice_success_;
  std::chrono::steady_clock::time_point save_notice_until_{};
  std::string last_overlay_error_;
  std::chrono::steady_clock::time_point last_overlay_error_at_{};
  std::chrono::steady_clock::time_point last_slow_dashboard_at_{};
  std::optional<std::chrono::system_clock::time_point> last_successful_exit_at_;
};
}  // namespace steamvr_gesture_exit::app
