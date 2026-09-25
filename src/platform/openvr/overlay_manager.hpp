#pragma once
#include "app/runtime_interfaces.h"
#include "platform/openvr/d3d_overlay_renderer.hpp"
#include "core/diagnostics.hpp"
#include "core/types.hpp"
#include "openvr.h"
#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace bailout::platform::openvr {
enum class OverlayAction { Primary, Confirm, Dismiss, Cancel };
struct DashboardEvents {
  std::vector<core::DashboardAction> actions;
  float max_event_age_seconds = 0;
  bool entered_button = false;
  bool hover_changed = false;
  bool tab_changed = false;
};
class OverlayManager {
 public:
  explicit OverlayManager(vr::IVROverlay* overlays, core::DiagnosticLog* log = nullptr)
      : overlays_(overlays), log_(log) {}
  bool initialize();
  void shutdown();
  [[nodiscard]] bool show_wrist(core::Hand configured_hand, const SurfaceModel&);
  [[nodiscard]] bool hide_wrist();
  [[nodiscard]] bool show_hud(const SurfaceModel&);
  [[nodiscard]] bool hide_hud();
  [[nodiscard]] bool update_dashboard(const DashboardSurfaceModel&);
  [[nodiscard]] bool dashboard_visible() const {
    return overlays_ && dashboard_ != vr::k_ulOverlayHandleInvalid &&
           overlays_->IsOverlayVisible(dashboard_);
  }
  [[nodiscard]] bool update_thumbnail(const std::wstring& title, const std::wstring& description,
                                      const std::string& friendly_name);
  std::vector<OverlayAction> poll_wrist_events(const app::HandPose& opposite_hand, bool confirming,
                                               bool counting_down);
  DashboardEvents poll_dashboard_events();
  bool wrist_visible() const {
    return wrist_visible_;
  }
  const std::string& error() const {
    return error_;
  }
  std::string take_error() {
    auto result = std::move(error_);
    error_.clear();
    return result;
  }

 private:
  bool submit(vr::VROverlayHandle_t, const std::string&, const SurfaceModel&);
  bool submit_dashboard();
  bool refresh_recreated_textures();
  bool fail(const char* operation, vr::EVROverlayError result);
  bool fail(const char* operation);
  vr::IVROverlay* overlays_ = nullptr;
  core::DiagnosticLog* log_ = nullptr;
  std::chrono::steady_clock::time_point last_pointer_log_at_{};
  std::chrono::steady_clock::time_point last_dashboard_submit_log_at_{};
  D3dOverlayRenderer renderer_;
  vr::VROverlayHandle_t dashboard_ = vr::k_ulOverlayHandleInvalid,
                        thumbnail_ = vr::k_ulOverlayHandleInvalid,
                        wrist_ = vr::k_ulOverlayHandleInvalid, hud_ = vr::k_ulOverlayHandleInvalid;
  bool wrist_visible_ = false;
  bool dashboard_texture_initialized_ = false;
  bool dashboard_texture_dirty_ = true;
  bool dashboard_was_visible_ = false;
  bool dashboard_pointer_primed_ = false;
  core::DashboardTab dashboard_tab_ = core::DashboardTab::Settings;
  bool thumbnail_requested_ = false;
  uint64_t renderer_generation_seen_ = 0;
  std::wstring thumbnail_title_, thumbnail_description_;
  SurfaceModel hud_model_{};
  std::string error_;
  std::optional<core::Hand> wrist_side_;
  uint32_t pointer_device_ = vr::k_unTrackedDeviceIndexInvalid;
  bool trigger_state_primed_ = false;
  bool previous_trigger_pressed_ = false;
  SurfaceModel wrist_model_{};
  DashboardSurfaceModel dashboard_model_{};
};
}  // namespace bailout::platform::openvr
