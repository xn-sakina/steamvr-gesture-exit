#include "core/state_machine.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace bailout::core {

bool Prerequisites::ready() const noexcept {
  return current_vr_game && configured_hand_tracked && opposite_pointer_available &&
         exit_target && exit_target->valid();
}

std::string_view to_string(const FlowState value) noexcept {
  switch (value) {
    case FlowState::NoActiveFlow: return "no_active_flow";
    case FlowState::IdleVisible: return "idle_visible";
    case FlowState::Confirming: return "confirming";
    case FlowState::CountingDown: return "counting_down";
    case FlowState::Exiting: return "exiting";
  }
  return "no_active_flow";
}

std::string_view to_string(const LastExitStatus value) noexcept {
  switch (value) {
    case LastExitStatus::Never: return "never";
    case LastExitStatus::Succeeded: return "succeeded";
    case LastExitStatus::Cancelled: return "cancelled";
    case LastExitStatus::Failed: return "failed";
    case LastExitStatus::TargetUnavailable: return "target_unavailable";
  }
  return "never";
}

FlowStateMachine::FlowStateMachine(const std::chrono::seconds control_timeout,
                                   const std::chrono::seconds exit_grace_period)
    : control_timeout_(control_timeout), exit_grace_period_(exit_grace_period) {
  if (control_timeout_ <= std::chrono::seconds::zero() ||
      exit_grace_period_ <= std::chrono::seconds::zero()) {
    throw std::invalid_argument("flow durations must be positive");
  }
}

void FlowStateMachine::transition(const FlowState next, Effects& effects, std::string code) {
  const FlowState previous = state_;
  state_ = next;
  effects.push_back({EffectType::Diagnostic, {}, {}, 0.0, std::move(code), previous, next});
}

void FlowStateMachine::reset_flow() noexcept {
  prerequisites_ = {};
  visible_target_.reset();
  locked_target_.reset();
  force_requested_ = false;
}

std::chrono::milliseconds FlowStateMachine::remaining(const TimePoint now) const noexcept {
  if (now >= countdown_deadline_) return {};
  return std::chrono::duration_cast<std::chrono::milliseconds>(countdown_deadline_ - now);
}

FlowSnapshot FlowStateMachine::snapshot(const TimePoint now) const {
  FlowSnapshot result{state_, last_exit_, locked_target_, {}, 0.0, failure_hud_visible_,
                      last_error_code_};
  if (state_ == FlowState::CountingDown) {
    result.countdown_remaining = remaining(now);
    const auto total = countdown_deadline_ - countdown_started_;
    if (total > TimePoint::duration::zero()) {
      result.countdown_progress = std::clamp(
          static_cast<double>(result.countdown_remaining.count()) /
              static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(total).count()),
          0.0, 1.0);
    }
  }
  return result;
}

Effects FlowStateMachine::on_gesture(const TimePoint now, const Prerequisites& prerequisites) {
  Effects effects;
  if (!prerequisites.ready()) return effects;
  if (state_ == FlowState::NoActiveFlow) {
    prerequisites_ = prerequisites;
    visible_target_ = prerequisites.exit_target;
    control_deadline_ = now + control_timeout_;
    transition(FlowState::IdleVisible, effects, "FLOW_GESTURE_ACCEPTED");
    effects.insert(effects.begin(), {EffectType::ShowIdleControl});
  } else if (state_ == FlowState::IdleVisible && visible_target_ == prerequisites.exit_target) {
    prerequisites_ = prerequisites;
    control_deadline_ = now + control_timeout_;
    effects.push_back({EffectType::Diagnostic, {}, {}, 0.0, "FLOW_IDLE_TIMEOUT_REFRESHED",
                       state_, state_});
  }
  return effects;
}

Effects FlowStateMachine::on_control_clicked(const TimePoint) {
  Effects effects;
  if (state_ == FlowState::IdleVisible) {
    transition(FlowState::Confirming, effects, "FLOW_CONFIRMATION_OPENED");
    effects.insert(effects.begin(), {EffectType::ShowConfirmation, visible_target_});
  }
  return effects;
}

Effects FlowStateMachine::on_dismiss() {
  Effects effects;
  if (state_ == FlowState::IdleVisible || state_ == FlowState::Confirming) {
    effects.push_back({EffectType::HideControl});
    transition(FlowState::NoActiveFlow, effects, "FLOW_DISMISSED");
    reset_flow();
  }
  return effects;
}

Effects FlowStateMachine::on_confirm(const TimePoint now, const TargetIdentity& target,
                                    const std::chrono::seconds countdown_duration) {
  Effects effects;
  constexpr std::array allowed{1, 3, 5, 10, 15};
  const bool duration_allowed =
      std::find(allowed.begin(), allowed.end(), countdown_duration.count()) != allowed.end();
  if (state_ != FlowState::Confirming || !duration_allowed || !target.valid() ||
      !prerequisites_.ready() || prerequisites_.exit_target != target || visible_target_ != target) {
    return effects;
  }
  locked_target_ = target;
  countdown_started_ = now;
  countdown_deadline_ = now + countdown_duration;
  transition(FlowState::CountingDown, effects, "FLOW_EXIT_CONFIRMED");
  effects.insert(effects.begin(),
                 {EffectType::ShowCountdownHud, locked_target_,
                  std::chrono::duration_cast<std::chrono::milliseconds>(countdown_duration), 1.0});
  return effects;
}

Effects FlowStateMachine::on_cancel() {
  Effects effects;
  if (state_ == FlowState::CountingDown) {
    effects.push_back({EffectType::HideControl});
    effects.push_back({EffectType::HideHud});
    last_exit_ = LastExitStatus::Cancelled;
    transition(FlowState::NoActiveFlow, effects, "FLOW_COUNTDOWN_CANCELLED");
    reset_flow();
  }
  return effects;
}

Effects FlowStateMachine::on_prerequisites(const TimePoint,
                                           const Prerequisites& prerequisites) {
  Effects effects;
  prerequisites_ = prerequisites;
  if ((state_ == FlowState::IdleVisible || state_ == FlowState::Confirming) &&
      (!prerequisites.ready() || prerequisites.exit_target != visible_target_)) {
    effects.push_back({EffectType::HideControl});
    transition(FlowState::NoActiveFlow, effects, "FLOW_PREREQUISITE_LOST");
    reset_flow();
  } else if (state_ == FlowState::CountingDown &&
             (!prerequisites.current_vr_game || !prerequisites.exit_target ||
              prerequisites.exit_target != locked_target_)) {
    finish_target_unavailable(effects, "FLOW_TARGET_CHANGED_DURING_COUNTDOWN");
  }
  // Exiting deliberately ignores foreground changes. The process adapter must
  // revalidate only the locked identity and report through on_target_status.
  return effects;
}

void FlowStateMachine::finish_success(Effects& effects, std::string code) {
  effects.push_back({EffectType::HideControl});
  effects.push_back({EffectType::HideHud});
  last_exit_ = LastExitStatus::Succeeded;
  last_error_code_.clear();
  transition(FlowState::NoActiveFlow, effects, std::move(code));
  reset_flow();
}

void FlowStateMachine::finish_target_unavailable(Effects& effects, std::string code) {
  effects.push_back({EffectType::HideControl});
  effects.push_back({EffectType::HideHud});
  last_exit_ = LastExitStatus::TargetUnavailable;
  transition(FlowState::NoActiveFlow, effects, std::move(code));
  reset_flow();
}

void FlowStateMachine::finish_failure(const TimePoint now, Effects& effects, std::string code) {
  effects.push_back({EffectType::HideControl});
  effects.push_back({EffectType::ShowFailureHud, locked_target_, {}, 0.0, code});
  last_exit_ = LastExitStatus::Failed;
  last_error_code_ = code.empty() ? "EXIT_FORCE_CLOSE_FAILED" : code;
  failure_hud_visible_ = true;
  failure_hud_deadline_ = now + std::chrono::seconds{4};
  transition(FlowState::NoActiveFlow, effects, last_error_code_);
  reset_flow();
}

Effects FlowStateMachine::on_target_status(const TimePoint, const TargetStatus status) {
  Effects effects;
  if (state_ == FlowState::CountingDown && status != TargetStatus::AliveAndMatching) {
    finish_target_unavailable(effects, status == TargetStatus::Exited
                                          ? "FLOW_TARGET_EXITED_DURING_COUNTDOWN"
                                          : "FLOW_TARGET_IDENTITY_CHANGED");
  } else if (state_ == FlowState::Exiting && status == TargetStatus::Exited) {
    finish_success(effects, "EXIT_TARGET_EXITED");
  } else if (state_ == FlowState::Exiting && status == TargetStatus::IdentityMismatch) {
    finish_target_unavailable(effects, "EXIT_TARGET_IDENTITY_CHANGED");
  }
  return effects;
}

Effects FlowStateMachine::on_graceful_result(const TimePoint, const CloseRequestResult result,
                                             std::string error_code) {
  Effects effects;
  if (state_ != FlowState::Exiting) return effects;
  if (result == CloseRequestResult::TargetExited) {
    finish_success(effects, "EXIT_GRACEFUL_SUCCEEDED");
  } else {
    effects.push_back({EffectType::Diagnostic, locked_target_, {}, 0.0,
                       result == CloseRequestResult::Accepted
                           ? "EXIT_GRACEFUL_REQUESTED"
                           : (error_code.empty() ? "EXIT_GRACEFUL_REQUEST_FAILED" : error_code),
                       state_, state_});
  }
  return effects;
}

Effects FlowStateMachine::on_force_result(const TimePoint now, const CloseRequestResult result,
                                          std::string error_code) {
  Effects effects;
  if (state_ != FlowState::Exiting || !force_requested_) return effects;
  if (result == CloseRequestResult::Accepted || result == CloseRequestResult::TargetExited) {
    finish_success(effects, "EXIT_FORCE_CLOSE_SUCCEEDED");
  } else {
    finish_failure(now, effects,
                   error_code.empty() ? "EXIT_FORCE_CLOSE_FAILED" : std::move(error_code));
  }
  return effects;
}

Effects FlowStateMachine::tick(const TimePoint now) {
  Effects effects;
  if (failure_hud_visible_ && now >= failure_hud_deadline_) {
    failure_hud_visible_ = false;
    effects.push_back({EffectType::HideHud});
  }
  if (state_ == FlowState::IdleVisible && now >= control_deadline_) {
    effects.push_back({EffectType::HideControl});
    transition(FlowState::NoActiveFlow, effects, "FLOW_IDLE_TIMEOUT");
    reset_flow();
  } else if (state_ == FlowState::CountingDown) {
    if (now < countdown_deadline_) {
      const FlowSnapshot current = snapshot(now);
      effects.push_back({EffectType::UpdateCountdownHud, locked_target_,
                         current.countdown_remaining, current.countdown_progress});
    } else {
      transition(FlowState::Exiting, effects, "FLOW_COUNTDOWN_COMPLETE");
      effects.insert(effects.begin(), {EffectType::ShowExitingHud, locked_target_});
      effects.insert(effects.begin() + 1,
                     {EffectType::RequestGracefulClose, locked_target_});
      grace_deadline_ = now + exit_grace_period_;
    }
  } else if (state_ == FlowState::Exiting && !force_requested_ && now >= grace_deadline_) {
    force_requested_ = true;
    effects.push_back({EffectType::RequestForceClose, locked_target_});
    effects.push_back({EffectType::Diagnostic, locked_target_, {}, 0.0,
                       "EXIT_GRACE_PERIOD_EXPIRED", state_, state_});
  }
  return effects;
}

}  // namespace bailout::core
