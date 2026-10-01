#pragma once
#include "app/runtime_interfaces.h"
#include "openvr.h"
#include <array>
#include <string>
namespace steamvr_gesture_exit::platform::openvr {
class OpenVrRuntime {
 public:
  bool initialize(vr::EVRApplicationType type = vr::VRApplication_Overlay);
  void shutdown();
  app::TrackingFrame sample_tracking() const;
  vr::IVRSystem* system() const {
    return vr::VRSystem();
  }
  vr::IVRApplications* applications() const {
    return vr::VRApplications();
  }
  vr::IVROverlay* overlays() const {
    return vr::VROverlay();
  }
  const std::string& error() const {
    return error_;
  }

 private:
  struct PointerModelCache {
    std::string render_model;
    bool resolved = false;
    bool has_tip = false;
  };

  app::HandPose sample_hand(vr::ETrackedControllerRole role,
                            const vr::TrackedDevicePose_t* poses) const;
  bool sample_pointer_aim(vr::TrackedDeviceIndex_t device,
                          const vr::HmdMatrix34_t& device_to_absolute,
                          const vr::VRControllerState_t& controller_state,
                          app::HandPose& hand) const;
  bool initialized_ = false;
  mutable std::array<PointerModelCache, vr::k_unMaxTrackedDeviceCount> pointer_models_{};
  std::string error_;
};
}  // namespace steamvr_gesture_exit::platform::openvr
