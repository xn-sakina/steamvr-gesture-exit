#pragma once
#include "app/runtime_interfaces.h"
#include "platform/openvr/d3d_overlay_renderer.hpp"
#include "openvr.h"
#include <optional>
#include <string>
#include <vector>
namespace bailout::platform::openvr {
enum class OverlayAction { Primary,Confirm,Dismiss,Cancel };
class OverlayManager {
 public:
  explicit OverlayManager(vr::IVROverlay* overlays):overlays_(overlays){}
  bool initialize(); void shutdown();
  void show_wrist(uint32_t device,const SurfaceModel&);void hide_wrist();
  void show_hud(const SurfaceModel&);void hide_hud();
  void update_dashboard(const DashboardSurfaceModel&);
  std::vector<OverlayAction> poll_wrist_events(const app::HandPose& opposite_hand,bool confirming,bool counting_down);
  std::vector<core::DashboardAction> poll_dashboard_events();
  bool wrist_visible()const{return wrist_visible_;} const std::string& error()const{return error_;}
 private:
  bool submit(vr::VROverlayHandle_t,const std::string&,const SurfaceModel&);
  vr::IVROverlay* overlays_=nullptr;D3dOverlayRenderer renderer_;
  vr::VROverlayHandle_t dashboard_=vr::k_ulOverlayHandleInvalid,thumbnail_=vr::k_ulOverlayHandleInvalid,wrist_=vr::k_ulOverlayHandleInvalid,hud_=vr::k_ulOverlayHandleInvalid;
  bool wrist_visible_=false;std::string error_;
  SurfaceModel wrist_model_{};DashboardSurfaceModel dashboard_model_{};
};
}
