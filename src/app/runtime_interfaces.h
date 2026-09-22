#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace bailout::app {

enum class Hand { Left, Right };
struct Vec3 { float x = 0, y = 0, z = 0; };
struct HandPose {
  bool connected = false;
  bool tracked = false;
  // Pointer input is deliberately capability-based. A tracked pose alone is
  // not enough: the controller must expose a trigger and a readable legacy
  // state for the OpenVR overlay mouse path used by the MVP.
  bool legacy_input_readable = false;
  bool trigger_button_supported = false;
  std::uint32_t device_index = 0xFFFFFFFFU;
  Vec3 position{}, forward{}, up{}, right{};
  std::uint64_t buttons = 0;

  [[nodiscard]] constexpr bool pointer_available() const noexcept {
    return connected && tracked && legacy_input_readable &&
           trigger_button_supported;
  }

  [[nodiscard]] constexpr bool accepts_pointer_event(
      const std::uint32_t source_device_index) const noexcept {
    return pointer_available() && source_device_index == device_index;
  }
};
struct TrackingFrame {
  std::chrono::steady_clock::time_point sampled_at{};
  bool headset_tracked = false;
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
