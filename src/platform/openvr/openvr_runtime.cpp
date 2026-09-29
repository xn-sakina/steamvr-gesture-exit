#include "platform/openvr/openvr_runtime.hpp"
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
  out.connected = s->IsTrackedDeviceConnected(i) &&
                  s->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_Controller;
  // ControllerStateWithPose is aligned to an input update, which can be stale
  // throughout a button-free wave. Use the same current tracking frame as the
  // HMD so gesture motion and the opposite-hand pointer ray continue to update.
  const auto& pose = poses[i];
  out.pose_valid = pose.bPoseIsValid;
  out.tracking_result = static_cast<int>(pose.eTrackingResult);
  out.tracked = out.connected && pose.bPoseIsValid &&
                pose.eTrackingResult == vr::TrackingResult_Running_OK;
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
    out.trigger_pressed = out.trigger_available &&
                          (state.ulButtonPressed &
                           vr::ButtonMaskFromId(vr::k_EButton_SteamVR_Trigger)) != 0;
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
    f.headset_tracked = f.headset_connected && h.bPoseIsValid &&
                        h.eTrackingResult == vr::TrackingResult_Running_OK;
    if (f.headset_tracked) {
      f.headset_position = position(h.mDeviceToAbsoluteTracking);
      f.headset_forward = axis(h.mDeviceToAbsoluteTracking, 2, -1);
    }
  }
  return f;
}
}  // namespace steamvr_gesture_exit::platform::openvr
