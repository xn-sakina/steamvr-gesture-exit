#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace steamvr_gesture_exit::core {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

struct Vec3 {
  double x{};
  double y{};
  double z{};
};

constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

constexpr double dot(Vec3 a, Vec3 b) noexcept {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

double length(Vec3 value) noexcept;
Vec3 normalized(Vec3 value) noexcept;

enum class Hand { Left, Right };
enum class Language { Auto, English, ChineseSimplified };
enum class GestureSensitivity { Low, Normal, High };

// process_start_token must be captured from the OS process creation time. It is
// deliberately part of identity so a recycled PID can never inherit an exit.
struct TargetIdentity {
  std::uint32_t process_id{};
  std::uint64_t process_start_token{};
  std::string executable_path;
  std::string display_name;

  [[nodiscard]] bool valid() const noexcept;
  friend bool operator==(const TargetIdentity&, const TargetIdentity&) = default;
};

}  // namespace steamvr_gesture_exit::core
