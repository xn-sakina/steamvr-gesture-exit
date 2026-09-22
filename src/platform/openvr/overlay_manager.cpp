#include "platform/openvr/overlay_manager.hpp"
#include <array>
#include <utility>
namespace bailout::platform::openvr {
namespace {
constexpr char kDashboardKey[] = "com.steamvr-bail-out.dashboard";
constexpr char kWristKey[] = "com.steamvr-bail-out.wrist";
constexpr char kHudKey[] = "com.steamvr-bail-out.hud";
vr::HmdMatrix34_t hud_transform() {
  vr::HmdMatrix34_t m = {{{1, 0, 0, 0.30f}, {0, 1, 0, 0.17f}, {0, 0, 1, -0.70f}}};
  return m;
}
}  // namespace
bool OverlayManager::fail(const char* operation, vr::EVROverlayError result) {
  error_ = std::string(operation) + ": " +
           (overlays_ ? overlays_->GetOverlayErrorNameFromEnum(result) : "IVROverlay unavailable") +
           " (" + std::to_string(static_cast<int>(result)) + ")";
  return false;
}
bool OverlayManager::fail(const char* operation) {
  error_ = operation;
  if (!renderer_.error().empty())
    error_ += ": " + renderer_.error();
  return false;
}
bool OverlayManager::initialize() {
  if (!overlays_)
    return fail("IVROverlay unavailable");
  if (!renderer_.initialize())
    return fail("renderer initialization failed");
  if (!renderer_.create_surface("dashboard", 1024, 640) ||
      !renderer_.create_surface("thumbnail", 256, 256) ||
      !renderer_.create_surface("wrist", 768, 384) || !renderer_.create_surface("hud", 768, 256))
    return fail("overlay texture creation failed");
  auto result = overlays_->CreateDashboardOverlay(kDashboardKey, "SteamVR Bail Out", &dashboard_,
                                                  &thumbnail_);
  if (result != vr::VROverlayError_None)
    return fail("CreateDashboardOverlay", result);
  result = overlays_->CreateOverlay(kWristKey, "Bail Out Wrist", &wrist_);
  if (result != vr::VROverlayError_None) {
    fail("CreateOverlay(wrist)", result);
    shutdown();
    return false;
  }
  result = overlays_->CreateOverlay(kHudKey, "Bail Out HUD", &hud_);
  if (result != vr::VROverlayError_None) {
    fail("CreateOverlay(hud)", result);
    shutdown();
    return false;
  }
  // Every renderer surface uses premultiplied alpha, including the thumbnail.
  for (const auto handle : {dashboard_, thumbnail_, wrist_, hud_}) {
    result = overlays_->SetOverlayFlag(handle, vr::VROverlayFlags_IsPremultiplied, true);
    if (result != vr::VROverlayError_None) {
      fail("SetOverlayFlag(IsPremultiplied)", result);
      shutdown();
      return false;
    }
  }
  auto configure = [this](vr::VROverlayHandle_t handle, float width,
                          vr::VROverlayInputMethod method, float px, float py) {
    auto result = overlays_->SetOverlayWidthInMeters(handle, width);
    if (result != vr::VROverlayError_None)
      return fail("SetOverlayWidthInMeters", result);
    result = overlays_->SetOverlayInputMethod(handle, method);
    if (result != vr::VROverlayError_None)
      return fail("SetOverlayInputMethod", result);
    if (method == vr::VROverlayInputMethod_Mouse) {
      vr::HmdVector2_t scale{{px, py}};
      result = overlays_->SetOverlayMouseScale(handle, &scale);
      if (result != vr::VROverlayError_None)
        return fail("SetOverlayMouseScale", result);
    }
    return true;
  };
  if (!configure(dashboard_, 1.5f, vr::VROverlayInputMethod_Mouse, 1024, 640) ||
      !configure(wrist_, 0.27f, vr::VROverlayInputMethod_Mouse, 768, 384) ||
      !configure(hud_, 0.42f, vr::VROverlayInputMethod_None, 0, 0)) {
    shutdown();
    return false;
  }
  for (auto flag : {vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible,
                    vr::VROverlayFlags_EnableClickStabilization, vr::VROverlayFlags_MultiCursor}) {
    result = overlays_->SetOverlayFlag(wrist_, flag, true);
    if (result != vr::VROverlayError_None) {
      fail("SetOverlayFlag(wrist)", result);
      shutdown();
      return false;
    }
  }
  auto hm = hud_transform();
  result = overlays_->SetOverlayTransformTrackedDeviceRelative(hud_, vr::k_unTrackedDeviceIndex_Hmd,
                                                               &hm);
  if (result != vr::VROverlayError_None) {
    fail("SetOverlayTransformTrackedDeviceRelative(hud)", result);
    shutdown();
    return false;
  }
  return true;
}
bool OverlayManager::update_thumbnail(const std::wstring& title, const std::wstring& description,
                                      const std::string& friendly_name) {
  if (dashboard_ == vr::k_ulOverlayHandleInvalid || thumbnail_ == vr::k_ulOverlayHandleInvalid)
    return fail("thumbnail overlay unavailable");
  auto result = overlays_->SetOverlayName(dashboard_, friendly_name.c_str());
  if (result != vr::VROverlayError_None)
    return fail("SetOverlayName(dashboard)", result);
  if (!renderer_.render_thumbnail("thumbnail", title, description))
    return fail("render thumbnail failed");
  vr::Texture_t texture{renderer_.texture("thumbnail"), vr::TextureType_DirectX,
                        vr::ColorSpace_Auto};
  result = overlays_->SetOverlayTexture(thumbnail_, &texture);
  return result == vr::VROverlayError_None || fail("SetOverlayTexture(thumbnail)", result);
}
void OverlayManager::shutdown() {
  if (!overlays_)
    return;
  for (auto h : {wrist_, hud_, dashboard_})
    if (h != vr::k_ulOverlayHandleInvalid)
      overlays_->DestroyOverlay(h);
  wrist_ = hud_ = dashboard_ = thumbnail_ = vr::k_ulOverlayHandleInvalid;
  wrist_visible_ = false;
  wrist_device_ = vr::k_unTrackedDeviceIndexInvalid;
  wrist_smoother_.reset();
}
bool OverlayManager::submit(vr::VROverlayHandle_t h, const std::string& id, const SurfaceModel& m) {
  if (h == vr::k_ulOverlayHandleInvalid)
    return fail("overlay handle unavailable");
  if (!renderer_.render(id, m))
    return fail("render overlay failed");
  vr::Texture_t t{renderer_.texture(id), vr::TextureType_DirectX, vr::ColorSpace_Auto};
  auto result = overlays_->SetOverlayTexture(h, &t);
  return result == vr::VROverlayError_None || fail("SetOverlayTexture", result);
}
bool OverlayManager::show_wrist(const app::HandPose& hand, core::TimePoint sampled_at,
                                const SurfaceModel& m) {
  if (!hand.tracked || hand.device_index == vr::k_unTrackedDeviceIndexInvalid)
    return fail("wrist controller is not tracked");
  bool same_content = wrist_model_.title == m.title && wrist_model_.body == m.body &&
                      wrist_model_.left_button == m.left_button &&
                      wrist_model_.right_button == m.right_button &&
                      wrist_model_.progress == m.progress;
  SurfaceModel next = m;
  if (same_content) {
    next.left_hover = wrist_model_.left_hover;
    next.right_hover = wrist_model_.right_hover;
  }
  if ((!same_content || !wrist_visible_) && !submit(wrist_, "wrist", next))
    return false;
  wrist_model_ = std::move(next);
  if (wrist_device_ != hand.device_index) {
    wrist_smoother_.reset();
    wrist_device_ = hand.device_index;
  }
  if (!update_wrist_pose(hand, sampled_at))
    return false;
  auto result = overlays_->ShowOverlay(wrist_);
  if (result != vr::VROverlayError_None)
    return fail("ShowOverlay(wrist)", result);
  wrist_visible_ = true;
  return true;
}
bool OverlayManager::update_wrist_pose(const app::HandPose& hand, core::TimePoint sampled_at) {
  if (wrist_device_ == vr::k_unTrackedDeviceIndexInvalid)
    return fail("wrist device unavailable");
  if (!hand.tracked || hand.device_index != wrist_device_) {
    (void)hide_wrist();
    return fail("wrist tracking lost");
  }
  const core::Vec3 right{hand.right.x, hand.right.y, hand.right.z};
  const core::Vec3 up{hand.up.x, hand.up.y, hand.up.z};
  const core::Vec3 back{-hand.forward.x, -hand.forward.y, -hand.forward.z};
  const core::Vec3 position{hand.position.x - 0.055 * up.x - 0.115 * back.x,
                            hand.position.y - 0.055 * up.y - 0.115 * back.y,
                            hand.position.z - 0.055 * up.z - 0.115 * back.z};
  const auto pose = wrist_smoother_.update({position, right, up, back}, sampled_at);
  vr::HmdMatrix34_t transform = {
      {{static_cast<float>(pose.right.x), static_cast<float>(pose.up.x),
        static_cast<float>(pose.back.x), static_cast<float>(pose.position.x)},
       {static_cast<float>(pose.right.y), static_cast<float>(pose.up.y),
        static_cast<float>(pose.back.y), static_cast<float>(pose.position.y)},
       {static_cast<float>(pose.right.z), static_cast<float>(pose.up.z),
        static_cast<float>(pose.back.z), static_cast<float>(pose.position.z)}}};
  auto result =
      overlays_->SetOverlayTransformAbsolute(wrist_, vr::TrackingUniverseStanding, &transform);
  return result == vr::VROverlayError_None || fail("SetOverlayTransformAbsolute(wrist)", result);
}
bool OverlayManager::hide_wrist() {
  bool ok = true;
  if (wrist_ != vr::k_ulOverlayHandleInvalid) {
    auto result = overlays_->HideOverlay(wrist_);
    if (result != vr::VROverlayError_None)
      ok = fail("HideOverlay(wrist)", result);
  }
  wrist_visible_ = false;
  wrist_device_ = vr::k_unTrackedDeviceIndexInvalid;
  wrist_smoother_.reset();
  return ok;
}
bool OverlayManager::show_hud(const SurfaceModel& m) {
  if (!submit(hud_, "hud", m))
    return false;
  auto result = overlays_->ShowOverlay(hud_);
  return result == vr::VROverlayError_None || fail("ShowOverlay(hud)", result);
}
bool OverlayManager::hide_hud() {
  if (hud_ == vr::k_ulOverlayHandleInvalid)
    return true;
  auto result = overlays_->HideOverlay(hud_);
  return result == vr::VROverlayError_None || fail("HideOverlay(hud)", result);
}
bool OverlayManager::submit_dashboard() {
  if (dashboard_ == vr::k_ulOverlayHandleInvalid)
    return fail("dashboard overlay unavailable");
  if (!renderer_.render_dashboard("dashboard", dashboard_model_))
    return fail("render dashboard failed");
  vr::Texture_t t{renderer_.texture("dashboard"), vr::TextureType_DirectX, vr::ColorSpace_Auto};
  auto result = overlays_->SetOverlayTexture(dashboard_, &t);
  return result == vr::VROverlayError_None || fail("SetOverlayTexture(dashboard)", result);
}
bool OverlayManager::update_dashboard(const DashboardSurfaceModel& m) {
  auto next = m;
  next.hovered_action = dashboard_model_.hovered_action;
  dashboard_model_ = std::move(next);
  return submit_dashboard();
}
std::vector<OverlayAction> OverlayManager::poll_wrist_events(const app::HandPose& hand,
                                                             bool confirming, bool counting) {
  std::vector<OverlayAction> out;
  bool left = wrist_model_.left_hover, right = wrist_model_.right_hover;
  vr::VREvent_t event{};
  while (overlays_->PollNextOverlayEvent(wrist_, &event, sizeof(event))) {
    if (event.eventType == vr::VREvent_FocusLeave) {
      left = false;
      right = false;
      continue;
    }
    if (!wrist_visible_ || !hand.accepts_pointer_event(event.trackedDeviceIndex))
      continue;
    if (event.eventType == vr::VREvent_MouseMove) {
      const float x = event.data.mouse.x, y = 384.0f - event.data.mouse.y;
      left = y >= 272 && y <= 330 && x >= 42 && x <= 372;
      right = y >= 272 && y <= 330 && x >= 396 && x <= 726;
    } else if (event.eventType == vr::VREvent_MouseButtonDown &&
               event.data.mouse.button == vr::VRMouseButton_Left) {
      if (counting) {
        if (right)
          out.push_back(OverlayAction::Cancel);
      } else if (confirming) {
        if (left)
          out.push_back(OverlayAction::Confirm);
        else if (right)
          out.push_back(OverlayAction::Dismiss);
      } else {
        if (left)
          out.push_back(OverlayAction::Primary);
        else if (right)
          out.push_back(OverlayAction::Dismiss);
      }
    }
  }
  if (!wrist_visible_ || !hand.pointer_available()) {
    left = false;
    right = false;
  }
  if (wrist_model_.left_hover != left || wrist_model_.right_hover != right) {
    wrist_model_.left_hover = left;
    wrist_model_.right_hover = right;
    if (!submit(wrist_, "wrist", wrist_model_))
      out.clear();
  }
  return out;
}
std::vector<core::DashboardAction> OverlayManager::poll_dashboard_events() {
  std::vector<core::DashboardAction> out;
  auto hovered = dashboard_model_.hovered_action;
  vr::VREvent_t e{};
  while (overlays_->PollNextOverlayEvent(dashboard_, &e, sizeof(e))) {
    if (e.eventType == vr::VREvent_FocusLeave) {
      hovered.reset();
      continue;
    }
    if (e.eventType != vr::VREvent_MouseMove && e.eventType != vr::VREvent_MouseButtonDown)
      continue;
    const float x = e.data.mouse.x, y = 640.0f - e.data.mouse.y;
    const auto hit = core::dashboard_hit_test(dashboard_model_.buttons, x, y);
    if (e.eventType == vr::VREvent_MouseMove)
      hovered = hit;
    else if (e.data.mouse.button == vr::VRMouseButton_Left && hit)
      out.push_back(*hit);
  }
  if (hovered != dashboard_model_.hovered_action) {
    dashboard_model_.hovered_action = hovered;
    if (!submit_dashboard())
      out.clear();
  }
  return out;
}
}  // namespace bailout::platform::openvr
