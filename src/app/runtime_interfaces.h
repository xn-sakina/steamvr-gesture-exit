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
  std::uint32_t device_index = 0xFFFFFFFFU;
  Vec3 position{}, forward{}, up{}, right{};
  std::uint64_t buttons = 0;
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
