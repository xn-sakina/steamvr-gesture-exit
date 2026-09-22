#include "core/wrist_pose_smoother.hpp"

#include <algorithm>
#include <cmath>

namespace bailout::core {
namespace {
constexpr double kTimeConstantSeconds = 0.075;
constexpr double kTrackingGapSeconds = 0.25;
constexpr double kJumpDistanceMeters = 0.5;

Vec3 scale(Vec3 value, double amount) noexcept {
  return {value.x * amount, value.y * amount, value.z * amount};
}

Vec3 add(Vec3 a, Vec3 b) noexcept {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 blend(Vec3 a, Vec3 b, double alpha) noexcept {
  return add(scale(a, 1.0 - alpha), scale(b, alpha));
}

Vec3 cross(Vec3 a, Vec3 b) noexcept {
  return {a.y * b.z - a.z * b.y,
          a.z * b.x - a.x * b.z,
          a.x * b.y - a.y * b.x};
}

WristPose orthonormalized(WristPose pose) noexcept {
  pose.right = normalized(pose.right);
  pose.up = normalized(pose.up - scale(pose.right, dot(pose.up, pose.right)));
  pose.back = normalized(cross(pose.right, pose.up));
  return pose;
}
}  // namespace

WristPose WristPoseSmoother::update(WristPose target, TimePoint time) noexcept {
  target = orthonormalized(target);
  const double elapsed = std::chrono::duration<double>(time - previous_time_).count();
  if (!pose_ || elapsed <= 0.0 || elapsed > kTrackingGapSeconds ||
      length(target.position - pose_->position) > kJumpDistanceMeters) {
    pose_ = target;
  } else {
    const double alpha = std::clamp(1.0 - std::exp(-elapsed / kTimeConstantSeconds), 0.0, 1.0);
    WristPose next{};
    next.position = blend(pose_->position, target.position, alpha);
    next.right = blend(pose_->right, target.right, alpha);
    next.up = blend(pose_->up, target.up, alpha);
    if (length(next.right) < 1e-6) next.right = target.right;
    if (length(next.up - scale(normalized(next.right),
                               dot(next.up, normalized(next.right)))) < 1e-6) {
      next.up = target.up;
    }
    pose_ = orthonormalized(next);
  }
  previous_time_ = time;
  return *pose_;
}

void WristPoseSmoother::reset() noexcept {
  pose_.reset();
  previous_time_ = {};
}

}  // namespace bailout::core
