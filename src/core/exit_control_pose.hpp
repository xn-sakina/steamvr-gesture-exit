#pragma once

#include "core/types.hpp"

namespace steamvr_gesture_exit::core {

struct ExitControlPose {
  Vec3 position;
  Vec3 right;
  Vec3 up;
  Vec3 back;
};

// Capture once when the Exit Control becomes visible. All inputs and the
// returned pose share the runtime's absolute tracking space.
[[nodiscard]] ExitControlPose exit_control_pose(Hand configured_hand, Vec3 head_position,
                                               Vec3 head_forward, Vec3 head_right) noexcept;

}  // namespace steamvr_gesture_exit::core
