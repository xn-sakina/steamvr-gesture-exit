#pragma once

#include "core/settings.hpp"

#include <optional>
#include <string>
#include <vector>

namespace bailout::core {

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

enum class DashboardAction {
  SetLeftHand,
  SetRightHand,
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

}  // namespace bailout::core
