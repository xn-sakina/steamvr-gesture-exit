#include "flow.h"

#include <algorithm>
#include <cmath>

namespace sbo {

namespace {
constexpr auto kGracePeriod = std::chrono::seconds(5);
constexpr auto kFailureNotice = std::chrono::seconds(3);
constexpr int kSupportedDurations[] = {1, 3, 5, 10, 15};
}

Settings default_settings() { return {}; }

bool validate_settings(const Settings& settings) {
  const bool durationOk = std::find(std::begin(kSupportedDurations), std::end(kSupportedDurations),
                                    settings.exitCountdownDurationSeconds) != std::end(kSupportedDurations);
  return settings.schemaVersion == 1 && durationOk && settings.exitControlTimeoutSeconds >= 3 &&
         settings.exitControlTimeoutSeconds <= 30;
}

bool same_identity(const ExitTargetIdentity& a, const ExitTargetIdentity& b) {
  if (a.pid == 0 || b.pid == 0 || a.pid != b.pid) return false;
  if (a.creationTime == 0 || b.creationTime == 0 || a.creationTime != b.creationTime) return false;
  if (!a.executablePath.empty() && !b.executablePath.empty() && a.executablePath != b.executablePath) return false;
  if (!a.appKey.empty() && !b.appKey.empty() && a.appKey != b.appKey) return false;
  return true;
}

FlowController::FlowController(Settings settings) : settings_(validate_settings(settings) ? settings : default_settings()) {
  rebuild_render(Clock::now());
}

void FlowController::update_settings(const Settings& settings, TimePoint now) {
  const Hand previousHand = settings_.configuredHand;
  settings_ = validate_settings(settings) ? settings : default_settings();
  if (settings_.configuredHand != previousHand &&
      (state_ == FlowState::idleVisible || state_ == FlowState::confirming)) {
    clear_flow();
  }
  rebuild_render(now);
}

void FlowController::update_prerequisites(const Prerequisites& prerequisites, TimePoint now) {
  const auto oldTarget = prerequisites_.exitTarget;
  prerequisites_ = prerequisites;
  if (state_ == FlowState::idleVisible || state_ == FlowState::confirming) {
    if (!can_show_exit_control() || !oldTarget || !prerequisites.exitTarget ||
        !same_identity(*oldTarget, *prerequisites.exitTarget)) {
      clear_flow();
    }
  }
  rebuild_render(now);
}

bool FlowController::can_show_exit_control() const {
  return prerequisites_.currentGameAvailable && prerequisites_.exitTargetAvailable &&
         prerequisites_.configuredHandTracked && prerequisites_.oppositePointerAvailable &&
         prerequisites_.exitTarget.has_value();
}

FlowResult FlowController::farewell_gesture(TimePoint now) {
  std::vector<Command> commands;
  if (state_ == FlowState::none && can_show_exit_control()) {
    state_ = FlowState::idleVisible;
    confirmingCandidate_ = prerequisites_.exitTarget;
    idleDeadline_ = now + std::chrono::seconds(settings_.exitControlTimeoutSeconds);
    transition_to(FlowState::idleVisible, commands, "gesture_opened_idle");
  } else if (state_ == FlowState::idleVisible && can_show_exit_control()) {
    idleDeadline_ = now + std::chrono::seconds(settings_.exitControlTimeoutSeconds);
  }
  rebuild_render(now);
  return finish(std::move(commands));
}

FlowResult FlowController::idle_timeout(TimePoint now) {
  if (state_ == FlowState::idleVisible) clear_flow();
  rebuild_render(now);
  return finish();
}

FlowResult FlowController::primary_clicked(TimePoint now) {
  std::vector<Command> commands;
  if (state_ == FlowState::idleVisible && can_show_exit_control()) {
    confirmingCandidate_ = prerequisites_.exitTarget;
    transition_to(FlowState::confirming, commands, "idle_primary_clicked");
  }
  rebuild_render(now);
  return finish(std::move(commands));
}

FlowResult FlowController::dismiss_clicked(TimePoint now) {
  if (state_ == FlowState::idleVisible || state_ == FlowState::confirming) clear_flow();
  rebuild_render(now);
  return finish();
}

FlowResult FlowController::confirm_clicked(TimePoint now, const std::optional<ExitTargetIdentity>& revalidatedTarget) {
  std::vector<Command> commands;
  if (state_ != FlowState::confirming || !confirmingCandidate_ || !revalidatedTarget ||
      !same_identity(*confirmingCandidate_, *revalidatedTarget)) {
    clear_flow();
    rebuild_render(now);
    return finish();
  }

  lockedTarget_ = *revalidatedTarget;
  lockedDurationSeconds_ = settings_.exitCountdownDurationSeconds;
  countdownStart_ = now;
  countdownEnd_ = now + std::chrono::seconds(lockedDurationSeconds_);
  lastSoundSecond_ = lockedDurationSeconds_ + 1;
  lastExit_ = LastExitStatus::pending;
  transition_to(FlowState::countingDown, commands, "confirm_locked_target");
  rebuild_render(now);
  return finish(std::move(commands));
}

FlowResult FlowController::cancel_clicked(TimePoint now) {
  std::vector<Command> commands;
  if (state_ == FlowState::countingDown) {
    lastExit_ = LastExitStatus::canceled;
    commands.push_back({CommandKind::updateLastExit, "canceled", lockedTarget_});
    clear_flow();
  }
  rebuild_render(now);
  return finish(std::move(commands));
}

FlowResult FlowController::tracking_lost(TimePoint now) {
  prerequisites_.configuredHandTracked = false;
  if (state_ == FlowState::idleVisible || state_ == FlowState::confirming) clear_flow();
  rebuild_render(now);
  return finish();
}

FlowResult FlowController::pointer_lost(TimePoint now) {
  prerequisites_.oppositePointerAvailable = false;
  if (state_ == FlowState::idleVisible || state_ == FlowState::confirming) clear_flow();
  rebuild_render(now);
  return finish();
}

FlowResult FlowController::tracking_restored(TimePoint now) {
  prerequisites_.configuredHandTracked = true;
  rebuild_render(now);
  return finish();
}

FlowResult FlowController::pointer_restored(TimePoint now) {
  prerequisites_.oppositePointerAvailable = true;
  rebuild_render(now);
  return finish();
}

FlowResult FlowController::tick(TimePoint now) {
  std::vector<Command> commands;
  if (state_ == FlowState::idleVisible && now >= idleDeadline_) {
    clear_flow();
  } else if (state_ == FlowState::countingDown) {
    const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(countdownEnd_ - now).count();
    if (settings_.countdownSoundEnabled && remaining >= 0 && static_cast<int>(remaining) < lastSoundSecond_) {
      lastSoundSecond_ = static_cast<int>(remaining);
      commands.push_back({CommandKind::playCountdownTick, "countdown_tick", lockedTarget_});
    }
    if (now >= countdownEnd_) {
      transition_to(FlowState::exiting, commands, "countdown_elapsed");
      commands.push_back({CommandKind::requestGracefulClose, "request_graceful_close", lockedTarget_});
      graceDeadline_ = now + kGracePeriod;
    }
  } else if (state_ == FlowState::failureNotice && now >= failureDeadline_) {
    clear_flow();
  }
  rebuild_render(now);
  return finish(std::move(commands));
}

FlowResult FlowController::confirmed_target_gone_or_invalid(TimePoint now) {
  std::vector<Command> commands;
  if (state_ == FlowState::countingDown || state_ == FlowState::exiting) {
    lastExit_ = LastExitStatus::targetGone;
    commands.push_back({CommandKind::updateLastExit, "target_gone", lockedTarget_});
    clear_flow();
  }
  rebuild_render(now);
  return finish(std::move(commands));
}

FlowResult FlowController::graceful_close_succeeded(TimePoint now) {
  std::vector<Command> commands;
  if (state_ == FlowState::exiting) {
    lastExit_ = LastExitStatus::successGraceful;
    commands.push_back({CommandKind::updateLastExit, "success_graceful", lockedTarget_});
    clear_flow();
  }
  rebuild_render(now);
  return finish(std::move(commands));
}

FlowResult FlowController::grace_period_elapsed(TimePoint now) {
  std::vector<Command> commands;
  if (state_ == FlowState::exiting) {
    commands.push_back({CommandKind::requestForceClose, "request_force_close", lockedTarget_});
    graceDeadline_ = now + kGracePeriod;
  }
  rebuild_render(now);
  return finish(std::move(commands));
}

FlowResult FlowController::force_close_succeeded(TimePoint now) {
  std::vector<Command> commands;
  if (state_ == FlowState::exiting) {
    lastExit_ = LastExitStatus::successForced;
    commands.push_back({CommandKind::updateLastExit, "success_forced", lockedTarget_});
    clear_flow();
  }
  rebuild_render(now);
  return finish(std::move(commands));
}

FlowResult FlowController::force_close_failed(TimePoint now, std::string errorCode) {
  std::vector<Command> commands;
  if (state_ == FlowState::exiting) {
    lastExit_ = LastExitStatus::failed;
    failureDeadline_ = now + kFailureNotice;
    state_ = FlowState::failureNotice;
    commands.push_back({CommandKind::updateLastExit, std::move(errorCode), lockedTarget_});
  }
  rebuild_render(now);
  return finish(std::move(commands));
}

FlowResult FlowController::hud_error_elapsed(TimePoint now) {
  if (state_ == FlowState::failureNotice) clear_flow();
  rebuild_render(now);
  return finish();
}

void FlowController::set_hover(HoverRegion hover) {
  hover_ = hover;
  render_.hover = hover;
}

void FlowController::transition_to(FlowState next, std::vector<Command>& commands, const std::string& code) {
  state_ = next;
  commands.push_back({CommandKind::logStateTransition, code, lockedTarget_ ? lockedTarget_ : confirmingCandidate_});
}

void FlowController::clear_flow() {
  state_ = FlowState::none;
  confirmingCandidate_.reset();
  lockedTarget_.reset();
  lockedDurationSeconds_ = 0;
  hover_ = HoverRegion::none;
}

void FlowController::rebuild_render(TimePoint now) {
  render_ = {};
  render_.state = state_;
  render_.lastExit = lastExit_;
  render_.hover = hover_;
  render_.hudInteractive = false;

  const auto target = lockedTarget_ ? lockedTarget_ : confirmingCandidate_;
  if (target) render_.targetName = target->displayName.empty() ? target->executablePath : target->displayName;

  switch (state_) {
    case FlowState::none:
      break;
    case FlowState::idleVisible:
      render_.exitControlVisible = true;
      render_.exitControlInteractive = true;
      render_.title = "Bail Out";
      render_.body = "Point with opposite hand to confirm";
      break;
    case FlowState::confirming:
      render_.exitControlVisible = true;
      render_.exitControlInteractive = true;
      render_.title = "Confirm Game Exit";
      render_.body = "May force close if unresponsive";
      break;
    case FlowState::countingDown: {
      render_.hudVisible = true;
      render_.exitControlVisible = prerequisites_.configuredHandTracked && prerequisites_.oppositePointerAvailable;
      render_.exitControlInteractive = render_.exitControlVisible;
      render_.cancelVisible = render_.exitControlVisible;
      const auto totalMs = std::max(1LL, std::chrono::duration_cast<std::chrono::milliseconds>(countdownEnd_ - countdownStart_).count());
      const auto remainingMs = std::max(0LL, std::chrono::duration_cast<std::chrono::milliseconds>(countdownEnd_ - now).count());
      render_.progress = static_cast<double>(remainingMs) / static_cast<double>(totalMs);
      render_.secondsRemaining = static_cast<int>(std::ceil(remainingMs / 1000.0));
      render_.title = "Exiting in";
      render_.body = "Cancel from wrist control";
      render_.playCountdownSound = settings_.countdownSoundEnabled;
      break;
    }
    case FlowState::exiting:
      render_.hudVisible = true;
      render_.title = "Exiting...";
      render_.body = "Closing confirmed game";
      break;
    case FlowState::failureNotice:
      render_.hudVisible = true;
      render_.title = "Exit failed";
      render_.body = "Close the game manually";
      break;
  }
}

FlowResult FlowController::finish(std::vector<Command> commands) {
  return {render_, std::move(commands)};
}

const char* to_string(FlowState state) {
  switch (state) {
    case FlowState::none: return "none";
    case FlowState::idleVisible: return "idle_visible";
    case FlowState::confirming: return "confirming";
    case FlowState::countingDown: return "counting_down";
    case FlowState::exiting: return "exiting";
    case FlowState::failureNotice: return "failure_notice";
  }
  return "unknown";
}

const char* to_string(LastExitStatus status) {
  switch (status) {
    case LastExitStatus::none: return "none";
    case LastExitStatus::pending: return "pending";
    case LastExitStatus::canceled: return "canceled";
    case LastExitStatus::successGraceful: return "success_graceful";
    case LastExitStatus::successForced: return "success_forced";
    case LastExitStatus::targetGone: return "target_gone";
    case LastExitStatus::failed: return "failed";
  }
  return "unknown";
}

}  // namespace sbo
