#include "platform/openvr/overlay_manager.hpp"
#include "platform/windows/paths.hpp"
#include <array>
#include <filesystem>
#include <utility>
namespace bailout::platform::openvr {
namespace {
constexpr char kDashboardKey[] = "com.steamvr-bail-out.dashboard";
constexpr char kWristKey[] = "com.steamvr-bail-out.wrist";
constexpr char kHudKey[] = "com.steamvr-bail-out.hud";
constexpr float kDashboardWidth = 1024.0f;
constexpr float kDashboardHeight = 640.0f;
constexpr float kWristWidth = 768.0f;
constexpr float kWristHeight = 384.0f;
constexpr float kButtonTop = 272.0f;
constexpr float kButtonBottom = 330.0f;
constexpr float kLeftButtonLeft = 42.0f;
constexpr float kLeftButtonRight = 372.0f;
constexpr float kRightButtonLeft = 396.0f;
constexpr float kRightButtonRight = 726.0f;
enum class WristHit { None, Left, Right };
WristHit wrist_hit(const SurfaceModel& model, const float x, const float y) {
  if (y < kButtonTop || y > kButtonBottom)
    return WristHit::None;
  if (!model.left_button.empty() && x >= kLeftButtonLeft && x <= kLeftButtonRight)
    return WristHit::Left;
  if (!model.right_button.empty() && x >= kRightButtonLeft && x <= kRightButtonRight)
    return WristHit::Right;
  return WristHit::None;
}
WristHit wrist_hit(const SurfaceModel& model, const vr::VREvent_t& event) {
  return wrist_hit(model, event.data.mouse.x, kWristHeight - event.data.mouse.y);
}
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
  if (!renderer_.create_surface("dashboard", static_cast<uint32_t>(kDashboardWidth),
                                static_cast<uint32_t>(kDashboardHeight)) ||
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
  if (!configure(dashboard_, 1.9f, vr::VROverlayInputMethod_Mouse, kDashboardWidth,
                 kDashboardHeight) ||
      !configure(wrist_, 0.27f, vr::VROverlayInputMethod_Mouse, 768, 384) ||
      !configure(hud_, 0.42f, vr::VROverlayInputMethod_None, 0, 0)) {
    shutdown();
    return false;
  }
  result = overlays_->SetOverlayInputMethod(wrist_, vr::VROverlayInputMethod_None);
  if (result != vr::VROverlayError_None) {
    fail("SetOverlayInputMethod(wrist inactive)", result);
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
  // Use the packaged PNG for the Dashboard icon without depending on D3D.
  const auto package_root =
      platform::windows::executable_path().parent_path().parent_path().parent_path();
  const auto image_path = package_root / L"assets" / L"brand" / L"bailout.png";
  if (std::filesystem::is_regular_file(image_path)) {
    const auto utf8 = image_path.u8string();
    const std::string native(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    result = overlays_->SetOverlayFromFile(thumbnail_, native.c_str());
    if (result == vr::VROverlayError_None) {
      thumbnail_requested_ = false;
      return true;
    }
  }
  thumbnail_title_ = title;
  thumbnail_description_ = description;
  thumbnail_requested_ = true;
  if (!renderer_.render_thumbnail("thumbnail", title, description))
    return fail("render thumbnail failed");
  vr::Texture_t texture{renderer_.texture("thumbnail"), vr::TextureType_DirectX,
                        vr::ColorSpace_Auto};
  result = overlays_->SetOverlayTexture(thumbnail_, &texture);
  if (result != vr::VROverlayError_None)
    return fail("SetOverlayTexture(thumbnail)", result);
  return true;
}
void OverlayManager::shutdown() {
  if (!overlays_)
    return;
  for (auto h : {wrist_, hud_, dashboard_})
    if (h != vr::k_ulOverlayHandleInvalid)
      overlays_->DestroyOverlay(h);
  wrist_ = hud_ = dashboard_ = thumbnail_ = vr::k_ulOverlayHandleInvalid;
  wrist_visible_ = false;
  wrist_interactive_ = false;
  dashboard_texture_initialized_ = false;
  dashboard_texture_dirty_ = true;
  dashboard_was_visible_ = false;
  dashboard_pointer_primed_ = false;
  thumbnail_requested_ = false;
  renderer_generation_seen_ = renderer_.generation();
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
  const bool interaction_regions_changed =
      wrist_model_.left_button.empty() != m.left_button.empty() ||
      wrist_model_.right_button.empty() != m.right_button.empty();
  if (interaction_regions_changed &&
      (!set_wrist_interactive(false) || !update_wrist_intersection_mask(m)))
    return false;
  SurfaceModel next = m;
  if (wrist_visible_ && wrist_model_.left_button == m.left_button &&
      wrist_model_.right_button == m.right_button) {
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
  bool ok = set_wrist_interactive(false);
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
bool OverlayManager::update_wrist_intersection_mask(const SurfaceModel& model) {
  std::array<vr::VROverlayIntersectionMaskPrimitive_t, 2> masks{};
  uint32_t count = 0;
  auto add_rectangle = [&](const float left, const float right) {
    auto& mask = masks[count++];
    mask.m_nPrimitiveType = vr::OverlayIntersectionPrimitiveType_Rectangle;
    mask.m_Primitive.m_Rectangle = {left, kButtonTop, right - left, kButtonBottom - kButtonTop};
  };
  if (!model.left_button.empty())
    add_rectangle(kLeftButtonLeft, kLeftButtonRight);
  if (!model.right_button.empty())
    add_rectangle(kRightButtonLeft, kRightButtonRight);
  const auto result = overlays_->SetOverlayIntersectionMask(
      wrist_, masks.data(), count, sizeof(vr::VROverlayIntersectionMaskPrimitive_t));
  return result == vr::VROverlayError_None || fail("SetOverlayIntersectionMask(wrist)", result);
}
bool OverlayManager::set_wrist_interactive(const bool interactive) {
  if (wrist_interactive_ == interactive)
    return true;
  const auto method = interactive ? vr::VROverlayInputMethod_Mouse : vr::VROverlayInputMethod_None;
  const auto result = overlays_->SetOverlayInputMethod(wrist_, method);
  if (result != vr::VROverlayError_None)
    return fail("SetOverlayInputMethod(wrist)", result);
  wrist_interactive_ = interactive;
  return true;
}
bool OverlayManager::wrist_pointer_over_button(const app::HandPose& hand) const {
  if (!wrist_visible_ || !hand.pointer_available())
    return false;
  vr::VROverlayIntersectionParams_t parameters{};
  parameters.vSource = {{hand.position.x, hand.position.y, hand.position.z}};
  parameters.vDirection = {{hand.forward.x, hand.forward.y, hand.forward.z}};
  parameters.eOrigin = vr::TrackingUniverseStanding;
  vr::VROverlayIntersectionResults_t intersection{};
  if (!overlays_->ComputeOverlayIntersection(wrist_, &parameters, &intersection))
    return false;
  const float x = intersection.vUVs.v[0] * kWristWidth;
  const float y = (1.0f - intersection.vUVs.v[1]) * kWristHeight;
  return wrist_hit(wrist_model_, x, y) != WristHit::None;
}
bool OverlayManager::show_hud(const SurfaceModel& m) {
  if (!submit(hud_, "hud", m))
    return false;
  hud_model_ = m;
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
  if (result != vr::VROverlayError_None)
    return fail("SetOverlayTexture(dashboard)", result);
  dashboard_texture_initialized_ = true;
  dashboard_texture_dirty_ = false;
  return true;
}
bool OverlayManager::refresh_recreated_textures() {
  const auto generation = renderer_.generation();
  if (generation == renderer_generation_seen_)
    return true;
  // A device reset replaces every texture. A D2D target reset replaces one texture.
  // Refreshing all submitted overlays also covers either case without exposing
  // renderer internals to OpenVR.
  if (thumbnail_requested_) {
    if (!renderer_.render_thumbnail("thumbnail", thumbnail_title_, thumbnail_description_))
      return fail("render thumbnail after device recovery failed");
    vr::Texture_t t{renderer_.texture("thumbnail"), vr::TextureType_DirectX, vr::ColorSpace_Auto};
    const auto result = overlays_->SetOverlayTexture(thumbnail_, &t);
    if (result != vr::VROverlayError_None)
      return fail("SetOverlayTexture(thumbnail after recovery)", result);
  }
  if (wrist_visible_ && !submit(wrist_, "wrist", wrist_model_))
    return false;
  if (hud_ != vr::k_ulOverlayHandleInvalid && overlays_->IsOverlayVisible(hud_) &&
      !submit(hud_, "hud", hud_model_))
    return false;
  dashboard_texture_initialized_ = false;
  dashboard_texture_dirty_ = true;
  renderer_generation_seen_ = generation;
  return true;
}
bool OverlayManager::update_dashboard(const DashboardSurfaceModel& m) {
  const bool recovered = renderer_.retry_pending_recovery();
  if (!recovered)
    fail("renderer recovery failed");
  if (renderer_.generation() != renderer_generation_seen_) {
    dashboard_texture_initialized_ = false;
    dashboard_texture_dirty_ = true;
  }
  const bool refreshed = recovered && refresh_recreated_textures();
  auto next = m;
  next.hovered_action = dashboard_model_.hovered_action;
  if (next != dashboard_model_) {
    dashboard_model_ = std::move(next);
    dashboard_texture_dirty_ = true;
  }
  if ((!dashboard_texture_initialized_ || (dashboard_texture_dirty_ && dashboard_visible())) &&
      !submit_dashboard())
    return false;
  return recovered && refreshed;
}
std::vector<OverlayAction> OverlayManager::poll_wrist_events(const app::HandPose& hand,
                                                             bool confirming, bool counting) {
  std::vector<OverlayAction> out;
  if (!set_wrist_interactive(wrist_pointer_over_button(hand)))
    return out;
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
      const auto hit = wrist_hit(wrist_model_, event);
      left = hit == WristHit::Left;
      right = hit == WristHit::Right;
    } else if (event.eventType == vr::VREvent_MouseButtonDown &&
               event.data.mouse.button == vr::VRMouseButton_Left) {
      // The click carries its own position. Countdown text may be submitted
      // without a new MouseMove, so saved hover must never decide the action.
      const auto hit = wrist_hit(wrist_model_, event);
      left = hit == WristHit::Left;
      right = hit == WristHit::Right;
      if (counting) {
        if (hit == WristHit::Right)
          out.push_back(OverlayAction::Cancel);
      } else if (confirming) {
        if (hit == WristHit::Left)
          out.push_back(OverlayAction::Confirm);
        else if (hit == WristHit::Right)
          out.push_back(OverlayAction::Dismiss);
      } else {
        if (hit == WristHit::Left)
          out.push_back(OverlayAction::Primary);
        else if (hit == WristHit::Right)
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
DashboardEvents OverlayManager::poll_dashboard_events() {
  DashboardEvents out;
  auto hovered = dashboard_model_.hovered_action;
  const bool visible = dashboard_visible();
  if (!visible || !dashboard_was_visible_) {
    hovered.reset();
    dashboard_pointer_primed_ = false;
  }
  dashboard_was_visible_ = visible;
  bool pointer_seen = false;
  bool allow_hover_sound = dashboard_pointer_primed_;
  vr::HmdVector2_t mouse_scale{{kDashboardWidth, kDashboardHeight}};
  bool mouse_scale_loaded = false;
  vr::VREvent_t e{};
  while (overlays_->PollNextOverlayEvent(dashboard_, &e, sizeof(e))) {
    if (e.eventType == vr::VREvent_FocusLeave ||
        e.eventType == vr::VREvent_OverlayShown ||
        e.eventType == vr::VREvent_OverlayHidden) {
      hovered.reset();
      dashboard_pointer_primed_ = false;
      allow_hover_sound = false;
      continue;
    }
    if (!visible ||
        (e.eventType != vr::VREvent_MouseMove && e.eventType != vr::VREvent_MouseButtonDown))
      continue;
    if (!mouse_scale_loaded) {
      vr::HmdVector2_t current{};
      if (overlays_->GetOverlayMouseScale(dashboard_, &current) == vr::VROverlayError_None)
        mouse_scale = current;
      mouse_scale_loaded = true;
    }
    const auto point = core::dashboard_mouse_to_pixels(
        e.data.mouse.x, e.data.mouse.y, mouse_scale.v[0], mouse_scale.v[1], kDashboardWidth,
        kDashboardHeight);
    const auto hit = point ? core::dashboard_hit_test(dashboard_model_.buttons, point->x, point->y)
                           : std::nullopt;
    if (e.eventType == vr::VREvent_MouseMove) {
      pointer_seen = true;
      hovered = hit;
    } else if (e.data.mouse.button == vr::VRMouseButton_Left && hit)
      out.actions.push_back(*hit);
  }
  out.entered_button = allow_hover_sound && hovered &&
                       hovered != dashboard_model_.hovered_action;
  if (visible && pointer_seen)
    dashboard_pointer_primed_ = true;
  if (hovered != dashboard_model_.hovered_action) {
    dashboard_model_.hovered_action = hovered;
    dashboard_texture_dirty_ = true;
    out.hover_changed = true;
  }
  return out;
}
}  // namespace bailout::platform::openvr
