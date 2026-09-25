#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace bailout::app {

enum class Hand { Left, Right };
struct Vec3 {
  float x = 0, y = 0, z = 0;
};
struct HandPose {
  bool connected = false;
  bool tracked = false;
  bool pose_valid = false;
  int tracking_result = -1;
  std::uint32_t device_index = 0xFFFFFFFFU;
  Vec3 position{}, forward{}, up{}, right{};
  bool trigger_available = false;
  bool trigger_pressed = false;

  [[nodiscard]] constexpr bool pointer_available() const noexcept {
    // A ray only requires a tracked controller pose. Trigger sampling is kept
    // separate so a transient input-state failure does not hide the control or
    // its pointer feedback.
    return connected && tracked && device_index != 0xFFFFFFFFU;
  }
};
struct TrackingFrame {
  std::chrono::steady_clock::time_point sampled_at{};
  bool headset_connected = false;
  bool headset_tracked = false;
  bool headset_pose_valid = false;
  int headset_tracking_result = -1;
  Vec3 headset_position{}, headset_forward{};
  HandPose left{}, right{};
};
// Windows process creation FILETIME; PID alone is never an identity.
struct RuntimeTarget {
  std::uint32_t pid = 0;
  std::uint64_t creation_token = 0;
  std::string app_key;
  std::wstring executable_path, display_name;
};
struct PointerEvent {
  enum class Type { Move, Press, Release };
  Type type = Type::Move;
  float u = 0, v = 0;
};
}  // namespace bailout::app
