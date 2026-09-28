#pragma once

#include "core/types.hpp"
#include "core/state_machine.hpp"

#include <string_view>

namespace bailout::core {

enum class StringId {
  AppName,
  ExitGame,
  ConfirmTitle,
  Confirm,
  Dismiss,
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
  ControllerTracked,
  ControllerNotTracked,
  LastExit,
  Unavailable,
  Never,
  SettingsSaved,
  SettingsSaveFailed,
  QuickGuide,
  Left,
  Right,
  AutoLanguage,
  EnglishLanguage,
  ChineseLanguage,
  Ready,
  ResolvingCurrentGame,
  NoApplicationsInterface,
  NoSceneApplication,
  SceneProcessUnavailable,
  SelfProcessRejected,
  ApplicationKeyUnavailable,
  ApplicationMetadataUnavailable,
  UnsafeApplication,
  CurrentGameUnavailable,
  ProcessUnavailable,
  ProcessAccessDenied,
  ProcessIdentityUnavailable,
  ProcessPathUnavailable,
  UnsafeProcess,
  ThumbnailTitle,
  ThumbnailDescription,
  SecondsUnit,
  On,
  Off,
  LastExitSucceeded,
  LastExitCancelled,
  LastExitFailed,
  LastExitTargetUnavailable,
};

[[nodiscard]] Language resolve_language(Language configured, std::string_view steam_language,
                                        std::string_view os_language) noexcept;
[[nodiscard]] std::string_view localize(StringId id, Language resolved_language) noexcept;
[[nodiscard]] std::string_view localize_last_exit(LastExitStatus status,
                                                  Language resolved_language) noexcept;

}  // namespace bailout::core
