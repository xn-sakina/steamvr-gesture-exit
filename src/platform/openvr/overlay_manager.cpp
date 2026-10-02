#include "platform/openvr/overlay_manager.hpp"
#include "platform/windows/paths.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <utility>
namespace steamvr_gesture_exit::platform::openvr {
namespace {
constexpr char kDashboardKey[] = "dev.kanamio.steamvrgestureexit.dashboard";
constexpr char kWristKey[] = "dev.kanamio.steamvrgestureexit.wrist";
constexpr char kPointerRayKey[] = "dev.kanamio.steamvrgestureexit.pointer-ray";
constexpr char kHudKey[] = "dev.kanamio.steamvrgestureexit.hud";
constexpr float kDashboardWidth = 1024.0f;
// The visible dashboard content starts 24 px from the top and ends at y=432.
// Keep the same 24 px of canvas space below the final settings row.
constexpr float kDashboardHeight = 456.0f;
constexpr float kDashboardWidthMeters = 2.52f;  // 90% of the previous 2.8-metre panel.
// The dialog keeps its former physical width. The wider transparent canvas
// only makes room for the avatar beside it.
constexpr float kWristWidth = 640.0f;
constexpr float kWristHeight = 512.0f;
constexpr float kWristWidthMeters = 0.275f;
constexpr float kPanelLeftWithLeftAvatar = 138.0f;
constexpr float kPanelLeftWithRightAvatar = 10.0f;
constexpr float kButtonTop = 400.0f;
constexpr float kButtonBottom = 468.0f;
constexpr auto kPromptEntranceDuration = std::chrono::milliseconds(220);
constexpr auto kCountdownEntranceDuration = std::chrono::milliseconds(300);
constexpr uint32_t kPointerRayTextureWidth = 16;
constexpr uint32_t kPointerRayTextureHeight = 1024;
constexpr float kPointerRaySourceFadeFraction = 0.07f;
constexpr size_t kMaxOverlayEventsPerPoll = 256;
enum class WristHit { None, Left, Right };
struct Float3 {
  float x = 0, y = 0, z = 0;
};
Float3 operator+(Float3 a, Float3 b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}
Float3 operator-(Float3 a, Float3 b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}
Float3 operator*(Float3 value, float scale) {
  return {value.x * scale, value.y * scale, value.z * scale};
}
float length(Float3 value) {
  return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}
float dot(Float3 a, Float3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
Float3 normalized(Float3 value) {
  const float magnitude = length(value);
  return magnitude > 1e-5f ? value * (1.0f / magnitude) : Float3{};
}
Float3 cross(Float3 a, Float3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
#ifndef NDEBUG
const char* tab_name(core::DashboardTab tab) {
  switch (tab) {
    case core::DashboardTab::Settings:
      return "settings";
    case core::DashboardTab::Status:
      return "status";
    case core::DashboardTab::Help:
      return "help";
  }
  return "unknown";
}
const char* action_name(core::DashboardAction action) {
  using A = core::DashboardAction;
  switch (action) {
    case A::SetLeftHand:
      return "set_left_hand";
    case A::SetRightHand:
      return "set_right_hand";
    case A::SetSensitivityLow:
      return "set_sensitivity_low";
    case A::SetSensitivityNormal:
      return "set_sensitivity_normal";
    case A::SetSensitivityHigh:
      return "set_sensitivity_high";
    case A::SetCountdown1:
      return "set_countdown_1";
    case A::SetCountdown3:
      return "set_countdown_3";
    case A::SetCountdown5:
      return "set_countdown_5";
    case A::SetCountdown10:
      return "set_countdown_10";
    case A::SetCountdown15:
      return "set_countdown_15";
    case A::DecreaseControlTimeout:
      return "decrease_control_timeout";
    case A::IncreaseControlTimeout:
      return "increase_control_timeout";
    case A::EnableCountdownSound:
      return "enable_countdown_sound";
    case A::DisableCountdownSound:
      return "disable_countdown_sound";
    case A::SetLanguageAuto:
      return "set_language_auto";
    case A::SetLanguageEnglish:
      return "set_language_english";
    case A::SetLanguageChinese:
      return "set_language_chinese";
  }
  return "unknown";
}
#endif
WristHit wrist_hit(const SurfaceModel& model, const float x, const float y) {
  if (model.mode != SurfaceMode::Countdown && model.animation_progress < 0.70f)
    return WristHit::None;
  if (y < kButtonTop || y > kButtonBottom)
    return WristHit::None;
  const float panel_left =
      model.avatar_on_right ? kPanelLeftWithRightAvatar : kPanelLeftWithLeftAvatar;
  const float left_button_left = panel_left + 30.0f;
  const float left_button_right = panel_left + 234.0f;
  const float right_button_left = panel_left + 258.0f;
  const float right_button_right = panel_left + 462.0f;
  if (!model.left_button.empty() && x >= left_button_left && x <= left_button_right)
    return WristHit::Left;
  const float right_left = model.mode == SurfaceMode::Countdown ? left_button_left
                                                                : right_button_left;
  if (!model.right_button.empty() && x >= right_left && x <= right_button_right)
    return WristHit::Right;
  return WristHit::None;
}
vr::HmdMatrix34_t hud_transform() {
  vr::HmdMatrix34_t m = {{{1, 0, 0, 0.30f}, {0, 1, 0, 0.17f}, {0, 0, 1, -0.70f}}};
  return m;
}
vr::HmdMatrix34_t control_transform(const core::Hand configured_hand,
                                    const app::TrackingFrame& frame) {
  Float3 forward = normalized({frame.headset_forward.x, 0.0f, frame.headset_forward.z});
  if (length(forward) < 0.5f)
    forward = {0.0f, 0.0f, -1.0f};
  const Float3 world_up{0.0f, 1.0f, 0.0f};
  const Float3 head_right = normalized(cross(forward, world_up));
  const float side = configured_hand == core::Hand::Left ? 0.28f : -0.28f;
  const Float3 head{frame.headset_position.x, frame.headset_position.y, frame.headset_position.z};
  const Float3 panel = head + head_right * side + forward * 0.68f + world_up * -0.08f;

  // Aim the panel back toward the eye position. This introduces a modest
  // inward yaw and upward pitch, instead of leaving a side panel parallel to
  // the HMD image plane. The absolute transform is captured only on first show.
  const Float3 z_axis = normalized(head - panel);
  const Float3 x_axis = normalized(cross(world_up, z_axis));
  const Float3 y_axis = normalized(cross(z_axis, x_axis));
  // The dialog centre remains at the legacy position while the transparent
  // canvas extends toward the avatar. A left-hand gesture presents on the
  // right and mirrors the avatar/message direction.
  const bool avatar_on_right = configured_hand == core::Hand::Left;
  constexpr float panel_centre_offset_metres = 0.0275f;
  const float local_offset = avatar_on_right ? -panel_centre_offset_metres
                                             : panel_centre_offset_metres;
  const Float3 overlay = panel - x_axis * local_offset;
  return {{{x_axis.x, y_axis.x, z_axis.x, overlay.x},
           {x_axis.y, y_axis.y, z_axis.y, overlay.y},
           {x_axis.z, y_axis.z, z_axis.z, overlay.z}}};
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
      !renderer_.create_surface("wrist", static_cast<uint32_t>(kWristWidth),
                                static_cast<uint32_t>(kWristHeight)) ||
      !renderer_.create_surface("hud", 768, 256))
    return fail("overlay texture creation failed");
  auto result = overlays_->CreateDashboardOverlay(kDashboardKey, "SteamVR Gesture Exit",
                                                  &dashboard_, &thumbnail_);
  if (result != vr::VROverlayError_None)
    return fail("CreateDashboardOverlay", result);
  result = overlays_->CreateOverlay(kWristKey, "SteamVR Gesture Exit Control", &wrist_);
  if (result != vr::VROverlayError_None) {
    fail("CreateOverlay(wrist)", result);
    shutdown();
    return false;
  }
  result =
      overlays_->CreateOverlay(kPointerRayKey, "SteamVR Gesture Exit Pointer Ray", &pointer_ray_);
  if (result != vr::VROverlayError_None) {
    fail("CreateOverlay(pointer ray)", result);
    shutdown();
    return false;
  }
  result = overlays_->CreateOverlay(kHudKey, "SteamVR Gesture Exit HUD", &hud_);
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
  if (!configure(dashboard_, kDashboardWidthMeters, vr::VROverlayInputMethod_Mouse, kDashboardWidth,
                 kDashboardHeight) ||
      !configure(wrist_, kWristWidthMeters, vr::VROverlayInputMethod_None, 0, 0) ||
      !configure(pointer_ray_, 0.005f, vr::VROverlayInputMethod_None, 0, 0) ||
      !configure(hud_, 0.42f, vr::VROverlayInputMethod_None, 0, 0)) {
    shutdown();
    return false;
  }
  // Keep most of the quad transparent and shape the visible core like a thin
  // illuminated tube. The texture top maps to local +Y (the panel end). Keep
  // its existing long fade, while the controller end gets only a short fade
  // over the first 7% of the ray so it emerges softly from the hand.
  std::array<uint8_t, kPointerRayTextureWidth * kPointerRayTextureHeight * 4> ray_pixels{};
  for (uint32_t y = 0; y < kPointerRayTextureHeight; ++y) {
    const float target_to_source =
        static_cast<float>(y) / static_cast<float>(kPointerRayTextureHeight - 1);
    const float target_fade_t = std::clamp((target_to_source - 0.12f) / 0.40f, 0.0f, 1.0f);
    const float target_alpha =
        target_fade_t * target_fade_t * (3.0f - 2.0f * target_fade_t);
    const float source_fade_t =
        std::clamp((1.0f - target_to_source) / kPointerRaySourceFadeFraction, 0.0f, 1.0f);
    const float source_alpha =
        source_fade_t * source_fade_t * (3.0f - 2.0f * source_fade_t);
    const float length_alpha = target_alpha * source_alpha;
    for (uint32_t x = 0; x < kPointerRayTextureWidth; ++x) {
      const float distance_from_centre =
          std::abs((static_cast<float>(x) + 0.5f) - kPointerRayTextureWidth * 0.5f);
      const float radius_position = distance_from_centre / 3.25f;
      const float tube_alpha = std::sqrt(std::max(0.0f, 1.0f - radius_position * radius_position));
      const auto alpha = static_cast<uint8_t>(
          std::lround(255.0f * std::clamp(length_alpha * tube_alpha, 0.0f, 1.0f)));
      const size_t offset = (static_cast<size_t>(y) * kPointerRayTextureWidth + x) * 4;
      ray_pixels[offset] = 0xFF;
      ray_pixels[offset + 1] = 0xFF;
      ray_pixels[offset + 2] = 0xFF;
      ray_pixels[offset + 3] = alpha;
    }
  }
  result = overlays_->SetOverlayRaw(pointer_ray_, ray_pixels.data(), kPointerRayTextureWidth,
                                    kPointerRayTextureHeight, 4);
  if (result != vr::VROverlayError_None) {
    fail("SetOverlayRaw(pointer ray)", result);
    shutdown();
    return false;
  }
  result = overlays_->SetOverlayColor(pointer_ray_, 0.20f, 0.74f, 0.94f);
  if (result == vr::VROverlayError_None)
    result = overlays_->SetOverlayAlpha(pointer_ray_, 0.92f);
  if (result != vr::VROverlayError_None) {
    fail("configure pointer ray appearance", result);
    shutdown();
    return false;
  }
  // This overlay draws and hit-tests its own pointer. Keeping Mouse input and
  // the global-interactive flag disabled prevents SteamVR from taking input
  // focus away from the running game merely because the pointer touched it.
  result =
      overlays_->SetOverlayFlag(wrist_, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, false);
  if (result != vr::VROverlayError_None) {
    fail("SetOverlayFlag(wrist non-interactive)", result);
    shutdown();
    return false;
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
  const auto image_path = package_root / L"assets" / L"brand" / L"steamvr-gesture-exit.png";
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
  renderer_.flush();
  if (result != vr::VROverlayError_None)
    return fail("SetOverlayTexture(thumbnail)", result);
  return true;
}
void OverlayManager::shutdown() {
  if (!overlays_)
    return;
  for (auto h : {pointer_ray_, wrist_, hud_, dashboard_})
    if (h != vr::k_ulOverlayHandleInvalid)
      overlays_->DestroyOverlay(h);
  pointer_ray_ = wrist_ = hud_ = dashboard_ = thumbnail_ = vr::k_ulOverlayHandleInvalid;
  wrist_visible_ = false;
  pointer_ray_visible_ = false;
  dashboard_texture_initialized_ = false;
  dashboard_texture_dirty_ = true;
  dashboard_was_visible_ = false;
  dashboard_show_refresh_armed_ = true;
  dashboard_pointer_primed_ = false;
  dashboard_tab_ = core::DashboardTab::Settings;
  thumbnail_requested_ = false;
  renderer_generation_seen_ = renderer_.generation();
  pointer_device_ = vr::k_unTrackedDeviceIndexInvalid;
  trigger_state_primed_ = false;
  previous_trigger_pressed_ = false;
}
bool OverlayManager::submit(vr::VROverlayHandle_t h, const std::string& id, const SurfaceModel& m) {
  if (h == vr::k_ulOverlayHandleInvalid)
    return fail("overlay handle unavailable");
  if (!renderer_.render(id, m))
    return fail("render overlay failed");
  vr::Texture_t t{renderer_.texture(id), vr::TextureType_DirectX, vr::ColorSpace_Auto};
  auto result = overlays_->SetOverlayTexture(h, &t);
  renderer_.flush();
  return result == vr::VROverlayError_None || fail("SetOverlayTexture", result);
}
bool OverlayManager::show_pointer_ray(const app::HandPose& hand, const app::Vec3& headset_position,
                                      const vr::HmdVector3_t& target) {
  if (pointer_ray_ == vr::k_ulOverlayHandleInvalid)
    return fail("pointer ray overlay unavailable");
  const auto pointer_origin = hand.pointer_origin();
  const Float3 source{pointer_origin.x, pointer_origin.y, pointer_origin.z};
  const Float3 endpoint{target.v[0], target.v[1], target.v[2]};
  const Float3 segment = endpoint - source;
  const float segment_length = length(segment);
  if (segment_length < 0.01f)
    return hide_pointer_ray();

  const Float3 y_axis = segment * (1.0f / segment_length);
  const Float3 centre = source + segment * 0.5f;
  const Float3 eye{headset_position.x, headset_position.y, headset_position.z};
  Float3 z_axis = eye - centre;
  z_axis = z_axis - y_axis * dot(z_axis, y_axis);
  if (length(z_axis) < 1e-4f) {
    z_axis = Float3{hand.up.x, hand.up.y, hand.up.z};
    z_axis = z_axis - y_axis * dot(z_axis, y_axis);
  }
  if (length(z_axis) < 1e-4f) {
    z_axis = Float3{0.0f, 1.0f, 0.0f};
    z_axis = z_axis - y_axis * dot(z_axis, y_axis);
  }
  if (length(z_axis) < 1e-4f)
    z_axis = {1.0f, 0.0f, 0.0f};
  z_axis = normalized(z_axis);
  const Float3 x_axis = normalized(cross(y_axis, z_axis));
  z_axis = normalized(cross(x_axis, y_axis));
  const vr::HmdMatrix34_t transform = {{{x_axis.x, y_axis.x, z_axis.x, centre.x},
                                        {x_axis.y, y_axis.y, z_axis.y, centre.y},
                                        {x_axis.z, y_axis.z, z_axis.z, centre.z}}};
  const float width = segment_length * static_cast<float>(kPointerRayTextureWidth) /
                      static_cast<float>(kPointerRayTextureHeight);
  auto result = overlays_->SetOverlayWidthInMeters(pointer_ray_, width);
  if (result != vr::VROverlayError_None)
    return fail("SetOverlayWidthInMeters(pointer ray)", result);
  result = overlays_->SetOverlayTransformAbsolute(pointer_ray_, vr::TrackingUniverseStanding,
                                                  &transform);
  if (result != vr::VROverlayError_None)
    return fail("SetOverlayTransformAbsolute(pointer ray)", result);
  if (!pointer_ray_visible_) {
    result = overlays_->ShowOverlay(pointer_ray_);
    if (result != vr::VROverlayError_None)
      return fail("ShowOverlay(pointer ray)", result);
  }
  pointer_ray_visible_ = true;
  return true;
}
bool OverlayManager::hide_pointer_ray() {
  if (!pointer_ray_visible_ || pointer_ray_ == vr::k_ulOverlayHandleInvalid)
    return true;
  const auto result = overlays_->HideOverlay(pointer_ray_);
  if (result != vr::VROverlayError_None)
    return fail("HideOverlay(pointer ray)", result);
  pointer_ray_visible_ = false;
  return true;
}
bool OverlayManager::show_wrist(const core::Hand configured_hand, const app::TrackingFrame& frame,
                                const SurfaceModel& m) {
  const auto now = std::chrono::steady_clock::now();
  SurfaceModel next = m;
  next.avatar_on_right = configured_hand == core::Hand::Left;
  const bool beginning_entrance = !wrist_visible_;
  const bool beginning_countdown =
      wrist_visible_ && m.mode == SurfaceMode::Countdown &&
      wrist_model_.mode != SurfaceMode::Countdown;
  if (beginning_entrance || beginning_countdown)
    wrist_animation_started_ = now;
  if (wrist_animation_started_ != std::chrono::steady_clock::time_point{}) {
    const auto duration = m.mode == SurfaceMode::Countdown ? kCountdownEntranceDuration
                                                           : kPromptEntranceDuration;
    next.animation_progress = std::clamp(
        std::chrono::duration<float>(now - wrist_animation_started_).count() /
            std::chrono::duration<float>(duration).count(),
        0.0f, 1.0f);
    if (next.animation_progress >= 1.0f)
      wrist_animation_started_ = {};
  }
  bool same_content = wrist_model_.title == next.title && wrist_model_.body == next.body &&
                      wrist_model_.left_button == next.left_button &&
                      wrist_model_.right_button == next.right_button &&
                      wrist_model_.progress == next.progress && wrist_model_.mode == next.mode &&
                      wrist_model_.timeout_progress == next.timeout_progress &&
                      wrist_model_.countdown_seconds == next.countdown_seconds &&
                      wrist_model_.animation_progress == next.animation_progress &&
                      wrist_model_.avatar_on_right == next.avatar_on_right;
  if (wrist_visible_) {
    next.pointer_visible = wrist_model_.pointer_visible;
    next.pointer_x = wrist_model_.pointer_x;
    next.pointer_y = wrist_model_.pointer_y;
    const auto hit =
        next.pointer_visible ? wrist_hit(next, next.pointer_x, next.pointer_y) : WristHit::None;
    next.left_hover = hit == WristHit::Left;
    next.right_hover = hit == WristHit::Right;
  }
  if ((!same_content || !wrist_visible_) && !submit(wrist_, "wrist", next))
    return false;
  wrist_model_ = std::move(next);
  if (!wrist_visible_) {
    auto transform = control_transform(configured_hand, frame);
    const auto transform_result =
        overlays_->SetOverlayTransformAbsolute(wrist_, vr::TrackingUniverseStanding, &transform);
    if (transform_result != vr::VROverlayError_None)
      return fail("SetOverlayTransformAbsolute(wrist)", transform_result);
  }
  auto result = overlays_->ShowOverlay(wrist_);
  if (result != vr::VROverlayError_None)
    return fail("ShowOverlay(wrist)", result);
  wrist_visible_ = true;
  return true;
}
bool OverlayManager::update_wrist_timeout(const float progress) {
  if (!wrist_visible_)
    return true;
  const float next = std::clamp(progress, 0.0f, 1.0f);
  if (std::abs(wrist_model_.timeout_progress - next) < 0.001f)
    return true;
  wrist_model_.timeout_progress = next;
  return submit(wrist_, "wrist", wrist_model_);
}
bool OverlayManager::hide_wrist() {
  bool ok = hide_pointer_ray();
  if (wrist_ != vr::k_ulOverlayHandleInvalid) {
    auto result = overlays_->HideOverlay(wrist_);
    if (result != vr::VROverlayError_None)
      ok = fail("HideOverlay(wrist)", result);
  }
  wrist_visible_ = false;
  pointer_device_ = vr::k_unTrackedDeviceIndexInvalid;
  trigger_state_primed_ = false;
  previous_trigger_pressed_ = false;
  wrist_model_.left_hover = false;
  wrist_model_.right_hover = false;
  wrist_model_.pointer_visible = false;
  wrist_animation_started_ = {};
  return ok;
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
  const auto render_started = std::chrono::steady_clock::now();
  if (!renderer_.render_dashboard("dashboard", dashboard_model_))
    return fail("render dashboard failed");
  const auto render_finished = std::chrono::steady_clock::now();
  vr::Texture_t t{renderer_.texture("dashboard"), vr::TextureType_DirectX, vr::ColorSpace_Auto};
  auto result = overlays_->SetOverlayTexture(dashboard_, &t);
  // SetOverlayTexture schedules SteamVR's copy from our D3D resource. Flushing
  // before the call lets that copy race the just-rendered commands and makes
  // the visible overlay trail input by one submission. Flush after the call so
  // hover entry and exit are presented in the frame they describe.
  renderer_.flush();
  const auto submit_finished = std::chrono::steady_clock::now();
  if (log_ && submit_finished - render_started >= std::chrono::milliseconds(16) &&
      submit_finished - last_dashboard_submit_log_at_ >= std::chrono::seconds(5)) {
    core::DiagnosticRecord record{core::DiagnosticEventType::Error, "DASHBOARD_TEXTURE_SLOW"};
    record.details = {
        {"render_ms", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         render_finished - render_started)
                                         .count())},
        {"submit_ms", std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                         submit_finished - render_finished)
                                         .count())}};
    (void)log_->append(record);
    last_dashboard_submit_log_at_ = submit_finished;
  }
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
    renderer_.flush();
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
bool OverlayManager::update_dashboard(const DashboardSurfaceModel& m,
                                      const bool force_texture_submit) {
  const bool recovered = renderer_.retry_pending_recovery();
  if (!recovered)
    fail("renderer recovery failed");
  if (renderer_.generation() != renderer_generation_seen_) {
    dashboard_texture_initialized_ = false;
    dashboard_texture_dirty_ = true;
  }
  const bool refreshed = recovered && refresh_recreated_textures();
  auto next = m;
  next.active_tab = dashboard_tab_;
  next.hovered_action = dashboard_model_.hovered_action;
  next.hovered_tab = dashboard_model_.hovered_tab;
  if (next != dashboard_model_) {
    dashboard_model_ = std::move(next);
    dashboard_texture_dirty_ = true;
  }
  const bool visible = dashboard_visible();
  // A show refresh is a one-shot submission request, not persistent dirty
  // state. If it fails, normal content changes keep their existing retry
  // behavior, while visibility alone cannot create an endless retry loop.
  if ((!dashboard_texture_initialized_ ||
       (visible && (dashboard_texture_dirty_ || force_texture_submit))) &&
      !submit_dashboard())
    return false;
  return recovered && refreshed;
}
WristEvents OverlayManager::poll_wrist_events(const app::HandPose& hand,
                                              const app::Vec3& headset_position,
                                              const bool pointer_enabled, bool confirming,
                                              bool counting) {
  WristEvents out;
  if (wrist_visible_ && wrist_model_.animation_progress < 1.0f &&
      wrist_animation_started_ != std::chrono::steady_clock::time_point{}) {
    const auto now = std::chrono::steady_clock::now();
    const auto duration = wrist_model_.mode == SurfaceMode::Countdown
                              ? kCountdownEntranceDuration
                              : kPromptEntranceDuration;
    const float progress = std::clamp(
        std::chrono::duration<float>(now - wrist_animation_started_).count() /
            std::chrono::duration<float>(duration).count(),
        0.0f, 1.0f);
    if (progress != wrist_model_.animation_progress) {
      wrist_model_.animation_progress = progress;
      if (!submit(wrist_, "wrist", wrist_model_))
        return out;
    }
    if (progress >= 1.0f)
      wrist_animation_started_ = {};
  }
  bool pointer_visible = false;
  float pointer_x = 0.0f, pointer_y = 0.0f;
  WristHit hit = WristHit::None;
  vr::HmdVector3_t pointer_target{};
  if (wrist_visible_ && pointer_enabled && hand.pointer_available()) {
    const auto pointer_origin = hand.pointer_origin();
    const auto pointer_direction = hand.pointer_direction();
    vr::VROverlayIntersectionParams_t parameters{};
    parameters.vSource = {{pointer_origin.x, pointer_origin.y, pointer_origin.z}};
    parameters.vDirection =
        {{pointer_direction.x, pointer_direction.y, pointer_direction.z}};
    parameters.eOrigin = vr::TrackingUniverseStanding;
    vr::VROverlayIntersectionResults_t intersection{};
    if (overlays_->ComputeOverlayIntersection(wrist_, &parameters, &intersection) &&
        intersection.vUVs.v[0] >= 0.0f && intersection.vUVs.v[0] <= 1.0f &&
        intersection.vUVs.v[1] >= 0.0f && intersection.vUVs.v[1] <= 1.0f) {
      pointer_visible = true;
      pointer_x = intersection.vUVs.v[0] * kWristWidth;
      pointer_y = (1.0f - intersection.vUVs.v[1]) * kWristHeight;
      pointer_target = intersection.vPoint;
      hit = wrist_hit(wrist_model_, pointer_x, pointer_y);
    }
  }

  const bool ray_updated = pointer_visible
                               ? show_pointer_ray(hand, headset_position, pointer_target)
                               : hide_pointer_ray();

  const auto next_pointer_device = pointer_enabled && hand.pointer_available()
                                       ? hand.device_index
                                       : vr::k_unTrackedDeviceIndexInvalid;
  if (pointer_device_ != next_pointer_device) {
    pointer_device_ = next_pointer_device;
    trigger_state_primed_ = false;
    previous_trigger_pressed_ = false;
  }
  bool trigger_rising = false;
  if (pointer_enabled && hand.pointer_available() && hand.trigger_available) {
    if (trigger_state_primed_)
      trigger_rising = hand.trigger_pressed && !previous_trigger_pressed_;
    trigger_state_primed_ = true;
    previous_trigger_pressed_ = hand.trigger_pressed;
  }
  if (trigger_rising && pointer_visible) {
    if (counting) {
      if (hit == WristHit::Right)
        out.actions.push_back(OverlayAction::Cancel);
    } else if (confirming) {
      if (hit == WristHit::Left)
        out.actions.push_back(OverlayAction::Confirm);
      else if (hit == WristHit::Right)
        out.actions.push_back(OverlayAction::Dismiss);
    } else {
      if (hit == WristHit::Left)
        out.actions.push_back(OverlayAction::Primary);
      else if (hit == WristHit::Right)
        out.actions.push_back(OverlayAction::Dismiss);
    }
  }

  const bool left = hit == WristHit::Left;
  const bool right = hit == WristHit::Right;
  const bool moved = pointer_visible != wrist_model_.pointer_visible ||
                     (pointer_visible && (std::abs(pointer_x - wrist_model_.pointer_x) >= 1.5f ||
                                          std::abs(pointer_y - wrist_model_.pointer_y) >= 1.5f));
  const bool hover_changed = wrist_model_.left_hover != left || wrist_model_.right_hover != right;
  out.entered_button = (left && !wrist_model_.left_hover) || (right && !wrist_model_.right_hover);
  out.clicked_button = !out.actions.empty();
  out.panel_hovered = pointer_visible;
  out.panel_hover_changed = pointer_visible != wrist_model_.pointer_visible;
  if (hover_changed || moved) {
    wrist_model_.left_hover = left;
    wrist_model_.right_hover = right;
    wrist_model_.pointer_visible = pointer_visible;
    wrist_model_.pointer_x = pointer_x;
    wrist_model_.pointer_y = pointer_y;
    if (!submit(wrist_, "wrist", wrist_model_)) {
      out = {};
    }
  }
  if (!ray_updated)
    out = {};
  return out;
}
DashboardEvents OverlayManager::poll_dashboard_events() {
  DashboardEvents out;
  auto hovered = dashboard_model_.hovered_action;
  auto hovered_tab = dashboard_model_.hovered_tab;
  const bool visible = dashboard_visible();
  auto request_texture_refresh = [&] {
    dashboard_show_refresh_armed_ = false;
    out.texture_refresh_requested = true;
  };
  // Refresh once for each hidden -> visible lifetime. The level check is the
  // authority; OverlayShown below also catches a hide/show cycle that occurred
  // entirely between two polls. The latch coalesces duplicate signals and
  // prevents SetOverlayTexture from causing a refresh loop.
  if (!visible)
    dashboard_show_refresh_armed_ = true;
  else if (dashboard_show_refresh_armed_)
    request_texture_refresh();
  if (!visible || !dashboard_was_visible_) {
    hovered.reset();
    hovered_tab.reset();
    dashboard_pointer_primed_ = false;
    dashboard_hold_repeater_.release();
  }
  if (!visible && dashboard_was_visible_ && dashboard_tab_ != core::DashboardTab::Settings) {
    dashboard_tab_ = core::DashboardTab::Settings;
    out.tab_changed = true;
  }
  dashboard_was_visible_ = visible;
  bool pointer_seen = false;
  bool allow_hover_sound = dashboard_pointer_primed_;
  vr::VREvent_t e{};
  for (size_t count = 0; count < kMaxOverlayEventsPerPoll &&
                         overlays_->PollNextOverlayEvent(dashboard_, &e, sizeof(e));
       ++count) {
    if (e.eventType == vr::VREvent_FocusLeave || e.eventType == vr::VREvent_OverlayShown ||
        e.eventType == vr::VREvent_OverlayHidden) {
      if (e.eventType == vr::VREvent_OverlayHidden) {
        dashboard_show_refresh_armed_ = true;
      } else if (e.eventType == vr::VREvent_OverlayShown && visible &&
                 dashboard_show_refresh_armed_) {
        request_texture_refresh();
      }
      hovered.reset();
      hovered_tab.reset();
      dashboard_pointer_primed_ = false;
      dashboard_hold_repeater_.release();
      allow_hover_sound = false;
      continue;
    }
    if (!visible ||
        (e.eventType != vr::VREvent_MouseMove && e.eventType != vr::VREvent_MouseButtonDown &&
         e.eventType != vr::VREvent_MouseButtonUp))
      continue;
    const auto point =
        core::dashboard_mouse_to_pixels(e.data.mouse.x, e.data.mouse.y, kDashboardWidth,
                                        kDashboardHeight, kDashboardWidth, kDashboardHeight);
    const auto tab_hit = point ? core::dashboard_tab_hit_test(point->x, point->y) : std::nullopt;
    const auto hit = point && dashboard_tab_ == core::DashboardTab::Settings && !tab_hit
                         ? core::dashboard_hit_test(dashboard_model_.buttons, point->x, point->y)
                         : std::nullopt;
    if (e.eventAgeSeconds > out.max_event_age_seconds)
      out.max_event_age_seconds = e.eventAgeSeconds;
#ifndef NDEBUG
    const auto event_now = std::chrono::steady_clock::now();
    if (log_ && (e.eventType == vr::VREvent_MouseButtonDown ||
                 ((hit != hovered || tab_hit != hovered_tab) &&
                  event_now - last_pointer_log_at_ >= std::chrono::milliseconds(150)) ||
                 event_now - last_pointer_log_at_ >= std::chrono::seconds(1))) {
      core::DiagnosticRecord record{};
      record.event = core::DiagnosticEventType::StateTransition;
      record.code = "DASHBOARD_POINTER_EVENT";
      record.debug_only = true;
      record.status = e.eventType == vr::VREvent_MouseMove       ? "move"
                      : e.eventType == vr::VREvent_MouseButtonUp ? "button_up"
                                                                 : "button_down";
      record.details = {
          {"device_index", std::to_string(e.trackedDeviceIndex)},
          {"raw_x", std::to_string(e.data.mouse.x)},
          {"raw_y", std::to_string(e.data.mouse.y)},
          {"mouse_scale_x", std::to_string(kDashboardWidth)},
          {"mouse_scale_y", std::to_string(kDashboardHeight)},
          {"texture_width", std::to_string(kDashboardWidth)},
          {"texture_height", std::to_string(kDashboardHeight)},
          {"active_tab", tab_name(dashboard_tab_)},
          {"hit_action", hit ? action_name(*hit) : "none"},
          {"hit_tab", tab_hit ? tab_name(*tab_hit) : "none"},
          {"converted_x", point ? std::to_string(point->x) : "invalid"},
          {"converted_y", point ? std::to_string(point->y) : "invalid"},
      };
      if (hit) {
        for (const auto& button : dashboard_model_.buttons) {
          if (button.action == *hit) {
            record.details["hit_bounds"] =
                std::to_string(button.bounds.left) + "," + std::to_string(button.bounds.top) + "," +
                std::to_string(button.bounds.right) + "," + std::to_string(button.bounds.bottom);
            break;
          }
        }
      }
      if (tab_hit) {
        for (const auto& tab : core::dashboard_tabs()) {
          if (tab.tab == *tab_hit) {
            record.details["hit_bounds"] =
                std::to_string(tab.bounds.left) + "," + std::to_string(tab.bounds.top) + "," +
                std::to_string(tab.bounds.right) + "," + std::to_string(tab.bounds.bottom);
            break;
          }
        }
      }
      (void)log_->append(record);
      last_pointer_log_at_ = event_now;
    }
#endif
    if (e.eventType == vr::VREvent_MouseMove) {
      pointer_seen = true;
      hovered = hit;
      hovered_tab = tab_hit;
    } else if (e.eventType == vr::VREvent_MouseButtonUp) {
      if (e.data.mouse.button == vr::VRMouseButton_Left)
        dashboard_hold_repeater_.release();
    } else if (e.data.mouse.button == vr::VRMouseButton_Left) {
      hovered = hit;
      hovered_tab = tab_hit;
      if (tab_hit && *tab_hit != dashboard_tab_) {
        dashboard_hold_repeater_.release();
        dashboard_tab_ = *tab_hit;
        out.tab_changed = true;
      } else if (hit) {
        out.actions.push_back(*hit);
        dashboard_hold_repeater_.press(*hit, std::chrono::steady_clock::now());
      }
    }
  }
  if (const auto repeated =
          dashboard_hold_repeater_.update(hovered, std::chrono::steady_clock::now()))
    out.actions.push_back(*repeated);
  out.entered_button =
      allow_hover_sound && ((hovered && hovered != dashboard_model_.hovered_action) ||
                            (hovered_tab && hovered_tab != dashboard_model_.hovered_tab));
  if (visible && pointer_seen)
    dashboard_pointer_primed_ = true;
  if (hovered != dashboard_model_.hovered_action) {
    dashboard_model_.hovered_action = hovered;
    dashboard_texture_dirty_ = true;
    out.hover_changed = true;
  }
  if (hovered_tab != dashboard_model_.hovered_tab) {
    dashboard_model_.hovered_tab = hovered_tab;
    dashboard_texture_dirty_ = true;
    out.hover_changed = true;
  }
  if (out.tab_changed) {
    dashboard_model_.active_tab = dashboard_tab_;
    dashboard_texture_dirty_ = true;
  }
  return out;
}
}  // namespace steamvr_gesture_exit::platform::openvr
