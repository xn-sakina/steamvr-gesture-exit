#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sbo {

enum class Hand { left, right };
enum class LanguageMode { autoDetect, english, chinese };
enum class FlowState { none, idleVisible, confirming, countingDown, exiting, failureNotice };
enum class LastExitStatus {
  none,
  pending,
  canceled,
  successGraceful,
  successForced,
  targetGone,
  failed
};
enum class HoverRegion { none, primary, dismiss, confirm, cancel };

struct Settings {
  int schemaVersion = 1;
  Hand configuredHand = Hand::right;
  int exitCountdownDurationSeconds = 5;
  int exitControlTimeoutSeconds = 8;
  bool countdownSoundEnabled = true;
  LanguageMode language = LanguageMode::autoDetect;
};

struct ExitTargetIdentity {
  std::uint32_t pid = 0;
  std::uint64_t creationTime = 0;
  std::string executablePath;
  std::string appKey;
  std::string displayName;
};

bool same_identity(const ExitTargetIdentity& a, const ExitTargetIdentity& b);

struct Prerequisites {
  bool currentGameAvailable = false;
  bool exitTargetAvailable = false;
  bool configuredHandTracked = false;
  bool oppositePointerAvailable = false;
  std::optional<ExitTargetIdentity> exitTarget;
  std::string unavailableReason = "no_current_game";
};

struct RenderModel {
  FlowState state = FlowState::none;
  LastExitStatus lastExit = LastExitStatus::none;
  bool exitControlVisible = false;
  bool exitControlInteractive = false;
  bool hudVisible = false;
  bool hudInteractive = false;
  bool cancelVisible = false;
  bool playCountdownSound = false;
  HoverRegion hover = HoverRegion::none;
  std::string title;
  std::string body;
  std::string targetName;
  std::string currentGameStatus;
  std::string exitTargetStatus;
  std::string controllerStatus;
  std::string settingsStatus;
  std::string languageCode;
  int secondsRemaining = 0;
  double progress = 0.0;
};

enum class CommandKind {
  logStateTransition,
  updateLastExit,
  requestGracefulClose,
  requestForceClose,
  playCountdownTick,
};

struct Command {
  CommandKind kind;
  std::string code;
  std::optional<ExitTargetIdentity> target;
};

struct FlowResult {
  RenderModel render;
  std::vector<Command> commands;
};

class FlowController {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  explicit FlowController(Settings settings = {});

  void update_settings(const Settings& settings, TimePoint now);
  void update_prerequisites(const Prerequisites& prerequisites, TimePoint now);
  FlowResult farewell_gesture(TimePoint now);
  FlowResult idle_timeout(TimePoint now);
  FlowResult primary_clicked(TimePoint now);
  FlowResult dismiss_clicked(TimePoint now);
  FlowResult confirm_clicked(TimePoint now, const std::optional<ExitTargetIdentity>& revalidatedTarget);
  FlowResult cancel_clicked(TimePoint now);
  FlowResult tracking_lost(TimePoint now);
  FlowResult pointer_lost(TimePoint now);
  FlowResult tracking_restored(TimePoint now);
  FlowResult pointer_restored(TimePoint now);
  FlowResult tick(TimePoint now);
  FlowResult confirmed_target_gone_or_invalid(TimePoint now);
  FlowResult graceful_close_succeeded(TimePoint now);
  FlowResult grace_period_elapsed(TimePoint now);
  FlowResult force_close_succeeded(TimePoint now);
  FlowResult force_close_failed(TimePoint now, std::string errorCode);
  FlowResult hud_error_elapsed(TimePoint now);

  void set_hover(HoverRegion hover);
  FlowState state() const { return state_; }
  LastExitStatus last_exit() const { return lastExit_; }
  const std::optional<ExitTargetIdentity>& locked_target() const { return lockedTarget_; }
  const RenderModel& render_model() const { return render_; }
  bool can_show_exit_control() const;

 private:
  FlowResult finish(std::vector<Command> commands = {});
  void transition_to(FlowState next, std::vector<Command>& commands, const std::string& code);
  void clear_flow();
  void rebuild_render(TimePoint now);

  Settings settings_;
  Prerequisites prerequisites_;
  FlowState state_ = FlowState::none;
  LastExitStatus lastExit_ = LastExitStatus::none;
  RenderModel render_;
  HoverRegion hover_ = HoverRegion::none;
  std::optional<ExitTargetIdentity> confirmingCandidate_;
  std::optional<ExitTargetIdentity> lockedTarget_;
  TimePoint idleDeadline_{};
  TimePoint countdownStart_{};
  TimePoint countdownEnd_{};
  TimePoint graceDeadline_{};
  TimePoint failureDeadline_{};
  int lockedDurationSeconds_ = 0;
  int lastSoundSecond_ = -1;
};

Settings default_settings();
bool validate_settings(const Settings& settings);
const char* to_string(FlowState state);
const char* to_string(LastExitStatus status);

}  // namespace sbo
