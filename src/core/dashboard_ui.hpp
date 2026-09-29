#pragma once

#include "core/settings.hpp"

#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace steamvr_gesture_exit::core {

struct DashboardRect {
  float left = 0;
  float top = 0;
  float right = 0;
  float bottom = 0;

  bool operator==(const DashboardRect&) const = default;

  [[nodiscard]] constexpr bool contains(float x, float y) const noexcept {
    return x >= left && x < right && y >= top && y < bottom;
  }
};

struct DashboardPoint {
  float x = 0;
  float y = 0;
};

enum class DashboardTab { Settings, Status, Help };

struct DashboardTabTarget {
  DashboardTab tab;
  DashboardRect bounds;
};

[[nodiscard]] std::array<DashboardTabTarget, 3> dashboard_tabs() noexcept;
[[nodiscard]] std::optional<DashboardTab> dashboard_tab_hit_test(float x, float y) noexcept;
[[nodiscard]] DashboardRect dashboard_timeout_value_rect() noexcept;

// OpenVR mouse events use a lower-left origin. Convert the runtime mouse scale
// to Direct2D's upper-left texture coordinates before testing button bounds.
[[nodiscard]] std::optional<DashboardPoint> dashboard_mouse_to_pixels(
    float x, float y, float mouse_width, float mouse_height, float texture_width,
    float texture_height) noexcept;

enum class DashboardAction {
  SetLeftHand,
  SetRightHand,
  SetSensitivityLow,
  SetSensitivityNormal,
  SetSensitivityHigh,
  SetCountdown1,
  SetCountdown3,
  SetCountdown5,
  SetCountdown10,
  SetCountdown15,
  DecreaseControlTimeout,
  IncreaseControlTimeout,
  EnableCountdownSound,
  DisableCountdownSound,
  SetLanguageAuto,
  SetLanguageEnglish,
  SetLanguageChinese,
};

enum class DashboardSettingRow {
  ConfiguredHand,
  GestureSensitivity,
  CountdownDuration,
  ControlTimeout,
  CountdownSound,
  Language,
};

struct DashboardButton {
  DashboardAction action{};
  DashboardSettingRow row{};
  DashboardRect bounds{};
  std::wstring label;
  bool selected = false;
  bool enabled = true;

  bool operator==(const DashboardButton&) const = default;
};

[[nodiscard]] std::vector<DashboardButton> dashboard_buttons(const Settings& settings);
[[nodiscard]] std::optional<DashboardAction> dashboard_hit_test(
    const std::vector<DashboardButton>& buttons, float x, float y) noexcept;
[[nodiscard]] bool apply_dashboard_action(Settings& settings, DashboardAction action) noexcept;

class DashboardHoldRepeater {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  void press(DashboardAction action, TimePoint now) noexcept;
  void release() noexcept;
  [[nodiscard]] std::optional<DashboardAction> update(std::optional<DashboardAction> hovered,
                                                      TimePoint now) noexcept;

 private:
  std::optional<DashboardAction> held_action_;
  TimePoint next_repeat_{};
};

}  // namespace steamvr_gesture_exit::core
