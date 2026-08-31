#pragma once

#include "core/types.hpp"

#include <string_view>

namespace bailout::core {

enum class StringId {
  AppName,
  ExitGame,
  ConfirmTitle,
  ConfirmBody,
  Confirm,
  Dismiss,
  CancelExit,
  Exiting,
  ExitFailed,
  CloseManually,
  SettingsTitle,
  ConfiguredHand,
  CountdownDuration,
  ControlTimeout,
  CountdownSound,
  LanguageLabel,
  HelpGesture,
  HelpOppositeHand,
  HelpForceClose,
  DiagnosticsTitle,
  CurrentVrGame,
  ExitTarget,
  HandTracking,
  OppositePointer,
  LastExit,
  Available,
  Unavailable,
  Never,
  SettingsRecovered,
};

[[nodiscard]] Language resolve_language(Language configured,
                                        std::string_view steam_language,
                                        std::string_view os_language) noexcept;
[[nodiscard]] std::string_view localize(StringId id, Language resolved_language) noexcept;

}  // namespace bailout::core
