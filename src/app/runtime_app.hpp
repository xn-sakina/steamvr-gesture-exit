#pragma once
#include "core/diagnostics.hpp"
#include "core/dashboard_ui.hpp"
#include "core/gesture_detector.hpp"
#include "core/localization.hpp"
#include "core/settings.hpp"
#include "core/state_machine.hpp"
#include "platform/openvr/openvr_runtime.hpp"
#include "platform/openvr/overlay_manager.hpp"
#include "platform/windows/countdown_sound.hpp"
#include "platform/windows/exit_target.hpp"
#include "platform/windows/process_controller.hpp"
#include <atomic>
#include <memory>
#include <optional>
namespace bailout::app {
class RuntimeApp {
 public:
  RuntimeApp();
  int run(); void request_stop(){stop_=true;}
 private:
  core::TargetIdentity core_target(const RuntimeTarget&)const;
  core::Prerequisites prerequisites(const TrackingFrame&,const platform::windows::TargetResolution&,bool dashboard_visible=false)const;
  void apply(core::Effects,std::chrono::steady_clock::time_point,const TrackingFrame&);
  void update_dashboard(const platform::windows::TargetResolution&,const TrackingFrame&,bool dashboard_visible);
  void dashboard_input(const std::vector<core::DashboardAction>&);
  void save_setting(const char*,const std::string&);
  std::wstring text(core::StringId)const;
  core::SettingsStore settings_store_;core::Settings settings_;core::Language language_=core::Language::English;
  core::DiagnosticLog log_;core::GestureDetector gesture_;std::unique_ptr<core::FlowStateMachine> flow_;
  platform::openvr::OpenVrRuntime vr_;std::unique_ptr<platform::openvr::OverlayManager> overlays_;
  std::unique_ptr<platform::windows::ExitTargetResolver> targets_;platform::windows::ProcessController process_;platform::windows::CountdownSound sound_;
  platform::windows::TargetResolution latest_resolution_{};std::optional<RuntimeTarget> locked_native_;int last_beep_second_=-1;std::atomic_bool stop_=false;
  std::string steam_language_,os_language_,availability_signature_;
  bool recovery_notice_pending_=false;
  std::chrono::steady_clock::time_point recovery_notice_until_{};
};
}
