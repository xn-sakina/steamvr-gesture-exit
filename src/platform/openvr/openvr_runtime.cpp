#include "platform/openvr/openvr_runtime.hpp"
#include <array>
#include <chrono>
#include <cmath>
namespace steamvr_gesture_exit::platform::openvr {
namespace {
app::Vec3 position(const vr::HmdMatrix34_t& m) {
  return {m.m[0][3], m.m[1][3], m.m[2][3]};
}
app::Vec3 axis(const vr::HmdMatrix34_t& m, int col, float sign = 1) {
  return {sign * m.m[0][col], sign * m.m[1][col], sign * m.m[2][col]};
}
vr::HmdMatrix34_t compose(const vr::HmdMatrix34_t& parent, const vr::HmdMatrix34_t& child) {
  vr::HmdMatrix34_t result{};
  for (int row = 0; row < 3; ++row) {
    for (int column = 0; column < 3; ++column) {
      for (int index = 0; index < 3; ++index)
        result.m[row][column] += parent.m[row][index] * child.m[index][column];
    }
    result.m[row][3] = parent.m[row][3];
    for (int index = 0; index < 3; ++index)
      result.m[row][3] += parent.m[row][index] * child.m[index][3];
  }
  return result;
}
float magnitude(const app::Vec3 value) {
  return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}
bool finite(const app::Vec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
}  // namespace
bool OpenVrRuntime::initialize(vr::EVRApplicationType type) {
  vr::EVRInitError e = vr::VRInitError_None;
  vr::VR_Init(&e, type);
  if (e != vr::VRInitError_None) {
    error_ = vr::VR_GetVRInitErrorAsEnglishDescription(e);
    return false;
  }
  initialized_ = vr::VRSystem() && vr::VRApplications();
  if (type == vr::VRApplication_Overlay)
    initialized_ = initialized_ && vr::VROverlay();
  if (!initialized_) {
    error_ = "required OpenVR interface unavailable";
    vr::VR_Shutdown();
  }
  return initialized_;
}
void OpenVrRuntime::shutdown() {
  if (initialized_)
    vr::VR_Shutdown();
  initialized_ = false;
  pointer_models_ = {};
}
bool OpenVrRuntime::sample_pointer_aim(const vr::TrackedDeviceIndex_t device,
                                       const vr::HmdMatrix34_t& device_to_absolute,
                                       const vr::VRControllerState_t& controller_state,
                                       app::HandPose& hand) const {
  auto* render_models = vr::VRRenderModels();
  if (!render_models || device >= pointer_models_.size())
    return false;

  auto& cache = pointer_models_[device];
  if (!cache.resolved) {
    std::array<char, 256> name{};
    vr::ETrackedPropertyError property_error = vr::TrackedProp_Success;
    const auto required = vr::VRSystem()->GetStringTrackedDeviceProperty(
        device, vr::Prop_RenderModelName_String, name.data(), static_cast<uint32_t>(name.size()),
        &property_error);
    cache.resolved = true;
    if (property_error == vr::TrackedProp_Success && required > 1 && required <= name.size()) {
      cache.render_model = name.data();
      cache.has_tip = render_models->RenderModelHasComponent(cache.render_model.c_str(), "tip");
    }
  }
  if (!cache.has_tip)
    return false;

  vr::RenderModel_ControllerMode_State_t mode{};
  vr::RenderModel_ComponentState_t component{};
  if (!render_models->GetComponentState(cache.render_model.c_str(), "tip", &controller_state, &mode,
                                        &component))
    return false;

  const auto tip_to_absolute = compose(device_to_absolute, component.mTrackingToComponentLocal);
  const auto aim_position = position(tip_to_absolute);
  auto aim_forward = axis(tip_to_absolute, 2, -1);
  const float aim_length = magnitude(aim_forward);
  if (!finite(aim_position) || !finite(aim_forward) || aim_length < 0.5f)
    return false;
  aim_forward = {aim_forward.x / aim_length, aim_forward.y / aim_length,
                 aim_forward.z / aim_length};
  hand.aim_position = aim_position;
  hand.aim_forward = aim_forward;
  hand.aim_pose_valid = true;
  return true;
}
app::HandPose OpenVrRuntime::sample_hand(vr::ETrackedControllerRole role,
                                         const vr::TrackedDevicePose_t* poses) const {
  app::HandPose out{};
  auto* s = vr::VRSystem();
  if (!s)
    return out;
  auto i = s->GetTrackedDeviceIndexForControllerRole(role);
  if (i == vr::k_unTrackedDeviceIndexInvalid || i >= vr::k_unMaxTrackedDeviceCount)
    return out;
  out.device_index = i;
  out.connected = s->IsTrackedDeviceConnected(i) &&
                  s->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Controller;
  // ControllerStateWithPose is aligned to an input update, which can be stale
  // throughout a button-free wave. Use the same current tracking frame as the
  // HMD so gesture motion and the opposite-hand pointer ray continue to update.
  const auto& pose = poses[i];
  out.pose_valid = pose.bPoseIsValid;
  out.tracking_result = static_cast<int>(pose.eTrackingResult);
  out.tracked =
      out.connected && pose.bPoseIsValid && pose.eTrackingResult == vr::TrackingResult_Running_OK;
  if (out.tracked) {
    out.position = position(pose.mDeviceToAbsoluteTracking);
    out.forward = axis(pose.mDeviceToAbsoluteTracking, 2, -1);
    out.up = axis(pose.mDeviceToAbsoluteTracking, 1);
    out.right = axis(pose.mDeviceToAbsoluteTracking, 0);
    // Poll the trigger without enabling overlay mouse input. SteamVR's
    // system-wide overlay laser can capture scene input focus, which games
    // such as VRChat may interpret as an AFK transition.
    vr::VRControllerState_t state{};
    out.trigger_available = s->GetControllerState(i, &state, sizeof(state));
    (void)sample_pointer_aim(i, pose.mDeviceToAbsoluteTracking, state, out);
    out.trigger_pressed =
        out.trigger_available &&
        (state.ulButtonPressed & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger)) != 0;
    if (out.trigger_available) {
      out.input_active = state.ulButtonPressed != 0;
      for (const auto& controller_axis : state.rAxis) {
        if (std::hypot(controller_axis.x, controller_axis.y) > 0.35f) {
          out.input_active = true;
          break;
        }
      }
    }
  }
  return out;
}
app::TrackingFrame OpenVrRuntime::sample_tracking() const {
  app::TrackingFrame f{};
  f.sampled_at = std::chrono::steady_clock::now();
  if (auto* s = vr::VRSystem()) {
    vr::TrackedDevicePose_t p[vr::k_unMaxTrackedDeviceCount]{};
    s->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0, p,
                                       vr::k_unMaxTrackedDeviceCount);
    f.left = sample_hand(vr::TrackedControllerRole_LeftHand, p);
    f.right = sample_hand(vr::TrackedControllerRole_RightHand, p);
    f.headset_connected = s->IsTrackedDeviceConnected(vr::k_unTrackedDeviceIndex_Hmd);
    auto& h = p[vr::k_unTrackedDeviceIndex_Hmd];
    f.headset_pose_valid = h.bPoseIsValid;
    f.headset_tracking_result = static_cast<int>(h.eTrackingResult);
    f.headset_tracked =
        f.headset_connected && h.bPoseIsValid && h.eTrackingResult == vr::TrackingResult_Running_OK;
    if (f.headset_tracked) {
      f.headset_position = position(h.mDeviceToAbsoluteTracking);
      f.headset_forward = axis(h.mDeviceToAbsoluteTracking, 2, -1);
      f.headset_right = axis(h.mDeviceToAbsoluteTracking, 0);
    }
  }
  return f;
}
}  // namespace steamvr_gesture_exit::platform::openvr
