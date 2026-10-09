#include "core/exit_control_pose.hpp"

#include <cmath>

namespace steamvr_gesture_exit::core {
namespace {
Vec3 add(Vec3 a, Vec3 b) noexcept {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 scale(Vec3 value, double amount) noexcept {
  return {value.x * amount, value.y * amount, value.z * amount};
}

Vec3 cross(Vec3 a, Vec3 b) noexcept {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
}  // namespace

ExitControlPose exit_control_pose(Hand configured_hand, Vec3 head_position,
                                  Vec3 head_forward, Vec3 head_right) noexcept {
  Vec3 forward = normalized(head_forward);
  if (length(forward) < 0.5)
    forward = {0.0, 0.0, -1.0};
  // Preserve pitch and roll: world-up and a flattened forward vector put the
  // control outside the intended view when looking down or lying on one side.
  Vec3 right = head_right - scale(forward, dot(head_right, forward));
  if (length(right) < 0.5) {
    const Vec3 reference = std::abs(forward.y) < 0.9 ? Vec3{0.0, 1.0, 0.0}
                                                   : Vec3{0.0, 0.0, -1.0};
    right = cross(forward, reference);
  }
  right = normalized(right);
  const Vec3 up = normalized(cross(right, forward));
  const double side = configured_hand == Hand::Left ? 0.28 : -0.28;
  const Vec3 panel = add(head_position,
                         add(scale(right, side), add(scale(forward, 0.68), scale(up, -0.08))));
  // Face the eyes, keeping the top of the panel aligned with the head's up.
  const Vec3 back = normalized(head_position - panel);
  const Vec3 panel_right = normalized(cross(up, back));
  const Vec3 panel_up = normalized(cross(back, panel_right));
  // Preserve the dialog centre while extending the transparent canvas toward
  // its avatar, mirrored for the configured hand.
  const double local_offset = configured_hand == Hand::Left ? -0.0275 : 0.0275;
  return {panel - scale(panel_right, local_offset), panel_right, panel_up, back};
}

}  // namespace steamvr_gesture_exit::core
