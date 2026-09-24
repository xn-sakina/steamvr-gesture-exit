#include "platform/openvr/openvr_runtime.hpp"
#include <chrono>
namespace bailout::platform::openvr {
namespace {
app::Vec3 position(const vr::HmdMatrix34_t& m) {
  return {m.m[0][3], m.m[1][3], m.m[2][3]};
}
app::Vec3 axis(const vr::HmdMatrix34_t& m, int col, float sign = 1) {
  return {sign * m.m[0][col], sign * m.m[1][col], sign * m.m[2][col]};
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
  out.connected = s->IsTrackedDeviceConnected(i);
  vr::ETrackedPropertyError button_error = vr::TrackedProp_Success;
  const auto supported_buttons =
      s->GetUint64TrackedDeviceProperty(i, vr::Prop_SupportedButtons_Uint64, &button_error);
  out.trigger_button_supported =
      button_error == vr::TrackedProp_Success &&
      (supported_buttons & vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger)) != 0;
  vr::VRControllerState_t state{};
  out.legacy_input_readable = s->GetControllerState(i, &state, sizeof(state));
  if (out.legacy_input_readable)
    out.buttons = state.ulButtonPressed;
  // ControllerStateWithPose is aligned to an input update, which can be stale
  // throughout a button-free wave. Use the same current tracking frame as the
  // HMD so gesture motion and wrist placement continue to update.
  const auto& pose = poses[i];
  out.tracked = out.connected && pose.bPoseIsValid &&
                pose.eTrackingResult == vr::TrackingResult_Running_OK;
  if (out.tracked) {
    out.position = position(pose.mDeviceToAbsoluteTracking);
    out.forward = axis(pose.mDeviceToAbsoluteTracking, 2, -1);
    out.up = axis(pose.mDeviceToAbsoluteTracking, 1);
    out.right = axis(pose.mDeviceToAbsoluteTracking, 0);
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
    f.headset_tracked = f.headset_connected && h.bPoseIsValid &&
                        h.eTrackingResult == vr::TrackingResult_Running_OK;
    if (f.headset_tracked) {
      f.headset_position = position(h.mDeviceToAbsoluteTracking);
      f.headset_forward = axis(h.mDeviceToAbsoluteTracking, 2, -1);
    }
  }
  return f;
}
}  // namespace bailout::platform::openvr
