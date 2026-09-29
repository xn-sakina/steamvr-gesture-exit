#include "core/types.hpp"

#include <cmath>

namespace steamvr_gesture_exit::core {

double length(const Vec3 value) noexcept {
  return std::sqrt(dot(value, value));
}

Vec3 normalized(const Vec3 value) noexcept {
  const double magnitude = length(value);
  if (magnitude <= 1e-9) {
    return {};
  }
  return {value.x / magnitude, value.y / magnitude, value.z / magnitude};
}

bool TargetIdentity::valid() const noexcept {
  return process_id != 0 && process_start_token != 0 && !executable_path.empty();
}

}  // namespace steamvr_gesture_exit::core
