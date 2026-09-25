#include "core/dashboard_ui.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace bailout::core {
namespace {

constexpr float kButtonLeft = 396.0F;
constexpr float kButtonRight = 956.0F;
constexpr float kRowTop = 174.0F;
constexpr float kRowHeight = 72.0F;
constexpr float kButtonInsetY = 6.0F;
constexpr float kButtonHeight = 60.0F;
constexpr float kButtonGap = 10.0F;

DashboardRect button_rect(const int row, const int column, const int count) {
  const float available = kButtonRight - kButtonLeft - kButtonGap * (count - 1);
  const float width = available / count;
  const float left = kButtonLeft + column * (width + kButtonGap);
  const float top = kRowTop + row * kRowHeight + kButtonInsetY;
  return {left, top, left + width, top + kButtonHeight};
}

void add_segment(std::vector<DashboardButton>& result, const DashboardSettingRow row,
                 const int row_index, const std::initializer_list<DashboardAction> actions,
                 const int selected_index) {
  const int count = static_cast<int>(actions.size());
  int column = 0;
  for (const auto action : actions) {
    result.push_back(
        {action, row, button_rect(row_index, column, count), {}, column == selected_index, true});
    ++column;
  }
}

}  // namespace

std::array<DashboardTabTarget, 3> dashboard_tabs() noexcept {
  return {{{DashboardTab::Settings, {48.0F, 88.0F, 346.0F, 148.0F}},
           {DashboardTab::Status, {363.0F, 88.0F, 661.0F, 148.0F}},
           {DashboardTab::Help, {678.0F, 88.0F, 976.0F, 148.0F}}}};
}

std::optional<DashboardTab> dashboard_tab_hit_test(const float x, const float y) noexcept {
  for (const auto& target : dashboard_tabs()) {
    if (target.bounds.contains(x, y))
      return target.tab;
  }
  return std::nullopt;
}

DashboardRect dashboard_timeout_value_rect() noexcept {
  return button_rect(2, 1, 3);
}

std::optional<DashboardPoint> dashboard_mouse_to_pixels(
    const float x, const float y, const float mouse_width, const float mouse_height,
    const float texture_width, const float texture_height) noexcept {
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(mouse_width) ||
      !std::isfinite(mouse_height) || mouse_width <= 0 || mouse_height <= 0 ||
      texture_width <= 0 || texture_height <= 0 || x < 0 || y < 0 || x > mouse_width ||
      y > mouse_height)
    return std::nullopt;
  // OpenVR mouse events use the lower-left origin; Direct2D button bounds use
  // the upper-left origin. The mouse scale is independent of the overlay's
  // physical size, including user resizing in the SteamVR Dashboard.
  return DashboardPoint{x * texture_width / mouse_width,
                        (mouse_height - y) * texture_height / mouse_height};
}

std::vector<DashboardButton> dashboard_buttons(const Settings& settings) {
  std::vector<DashboardButton> result;
  result.reserve(14);
  add_segment(result, DashboardSettingRow::ConfiguredHand, 0,
              {DashboardAction::SetLeftHand, DashboardAction::SetRightHand},
              settings.configured_hand == Hand::Left ? 0 : 1);

  constexpr std::array<int, 5> countdown_values{1, 3, 5, 10, 15};
  const auto countdown =
      std::find(countdown_values.begin(), countdown_values.end(), settings.countdown_seconds);
  const int countdown_index = countdown == countdown_values.end()
                                  ? 2
                                  : static_cast<int>(countdown - countdown_values.begin());
  add_segment(result, DashboardSettingRow::CountdownDuration, 1,
              {DashboardAction::SetCountdown1, DashboardAction::SetCountdown3,
               DashboardAction::SetCountdown5, DashboardAction::SetCountdown10,
               DashboardAction::SetCountdown15},
              countdown_index);

  const auto minus = button_rect(2, 0, 3);
  const auto plus = button_rect(2, 2, 3);
  result.push_back({DashboardAction::DecreaseControlTimeout,
                    DashboardSettingRow::ControlTimeout,
                    minus,
                    {},
                    false,
                    settings.control_timeout_seconds > 3});
  result.push_back({DashboardAction::IncreaseControlTimeout,
                    DashboardSettingRow::ControlTimeout,
                    plus,
                    {},
                    false,
                    settings.control_timeout_seconds < 30});

  add_segment(result, DashboardSettingRow::CountdownSound, 3,
              {DashboardAction::EnableCountdownSound, DashboardAction::DisableCountdownSound},
              settings.countdown_sound ? 0 : 1);
  const int language = settings.language == Language::Auto      ? 0
                       : settings.language == Language::English ? 1
                                                                : 2;
  add_segment(result, DashboardSettingRow::Language, 4,
              {DashboardAction::SetLanguageAuto, DashboardAction::SetLanguageEnglish,
               DashboardAction::SetLanguageChinese},
              language);
  return result;
}

std::optional<DashboardAction> dashboard_hit_test(const std::vector<DashboardButton>& buttons,
                                                  const float x, const float y) noexcept {
  for (const auto& button : buttons) {
    if (button.enabled && button.bounds.contains(x, y))
      return button.action;
  }
  return std::nullopt;
}

bool apply_dashboard_action(Settings& settings, const DashboardAction action) noexcept {
  const Settings before = settings;
  switch (action) {
    case DashboardAction::SetLeftHand:
      settings.configured_hand = Hand::Left;
      break;
    case DashboardAction::SetRightHand:
      settings.configured_hand = Hand::Right;
      break;
    case DashboardAction::SetCountdown1:
      settings.countdown_seconds = 1;
      break;
    case DashboardAction::SetCountdown3:
      settings.countdown_seconds = 3;
      break;
    case DashboardAction::SetCountdown5:
      settings.countdown_seconds = 5;
      break;
    case DashboardAction::SetCountdown10:
      settings.countdown_seconds = 10;
      break;
    case DashboardAction::SetCountdown15:
      settings.countdown_seconds = 15;
      break;
    case DashboardAction::DecreaseControlTimeout:
      settings.control_timeout_seconds = std::max(3, settings.control_timeout_seconds - 1);
      break;
    case DashboardAction::IncreaseControlTimeout:
      settings.control_timeout_seconds = std::min(30, settings.control_timeout_seconds + 1);
      break;
    case DashboardAction::EnableCountdownSound:
      settings.countdown_sound = true;
      break;
    case DashboardAction::DisableCountdownSound:
      settings.countdown_sound = false;
      break;
    case DashboardAction::SetLanguageAuto:
      settings.language = Language::Auto;
      break;
    case DashboardAction::SetLanguageEnglish:
      settings.language = Language::English;
      break;
    case DashboardAction::SetLanguageChinese:
      settings.language = Language::ChineseSimplified;
      break;
  }
  return settings != before;
}

}  // namespace bailout::core
