#pragma once
#include "app/runtime_interfaces.h"
#include "platform/openvr/d3d_overlay_renderer.hpp"
#include "core/wrist_pose_smoother.hpp"
#include "openvr.h"
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace bailout::platform::openvr {
enum class OverlayAction { Primary, Confirm, Dismiss, Cancel };
struct DashboardEvents {
  std::vector<core::DashboardAction> actions;
  bool entered_button = false;
  bool hover_changed = false;
};
class OverlayManager {
 public:
  explicit OverlayManager(vr::IVROverlay* overlays) : overlays_(overlays) {}
  bool initialize();
  void shutdown();
  [[nodiscard]] bool show_wrist(const app::HandPose& hand, core::TimePoint sampled_at,
                                const SurfaceModel&);
  [[nodiscard]] bool hide_wrist();
  [[nodiscard]] bool update_wrist_pose(const app::HandPose& hand, core::TimePoint sampled_at);
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
  bool update_wrist_intersection_mask(const SurfaceModel&);
  bool set_wrist_interactive(bool interactive);
  [[nodiscard]] bool wrist_pointer_over_button(const app::HandPose&) const;
  bool fail(const char* operation, vr::EVROverlayError result);
  bool fail(const char* operation);
  vr::IVROverlay* overlays_ = nullptr;
  D3dOverlayRenderer renderer_;
  vr::VROverlayHandle_t dashboard_ = vr::k_ulOverlayHandleInvalid,
                        thumbnail_ = vr::k_ulOverlayHandleInvalid,
                        wrist_ = vr::k_ulOverlayHandleInvalid, hud_ = vr::k_ulOverlayHandleInvalid;
  bool wrist_visible_ = false;
  bool wrist_interactive_ = false;
  bool dashboard_texture_initialized_ = false;
  bool dashboard_texture_dirty_ = true;
  bool dashboard_was_visible_ = false;
  bool dashboard_pointer_primed_ = false;
  bool thumbnail_requested_ = false;
  uint64_t renderer_generation_seen_ = 0;
  std::wstring thumbnail_title_, thumbnail_description_;
  SurfaceModel hud_model_{};
  std::string error_;
  uint32_t wrist_device_ = vr::k_unTrackedDeviceIndexInvalid;
  core::WristPoseSmoother wrist_smoother_;
  SurfaceModel wrist_model_{};
  DashboardSurfaceModel dashboard_model_{};
};
}  // namespace bailout::platform::openvr
