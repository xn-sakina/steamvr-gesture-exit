#pragma once
#include "app/runtime_interfaces.h"
#include "openvr.h"
#include <string>
namespace bailout::platform::openvr {
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
  app::HandPose sample_hand(vr::ETrackedControllerRole role,
                            const vr::TrackedDevicePose_t* poses) const;
  bool initialized_ = false;
  std::string error_;
};
}  // namespace bailout::platform::openvr
