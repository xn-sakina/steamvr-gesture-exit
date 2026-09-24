#pragma once

#include "core/types.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bailout::core {

enum class FlowState { NoActiveFlow, IdleVisible, Confirming, CountingDown, Exiting };
enum class LastExitStatus { Never, Succeeded, Cancelled, Failed, TargetUnavailable };
enum class TargetStatus { AliveAndMatching, Exited, IdentityMismatch };
enum class CloseRequestResult { Accepted, TargetExited, Failed };

struct Prerequisites {
  bool current_vr_game{};
  bool hardware_available{};
  bool headset_tracked{};
  bool configured_hand_tracked{};
  bool opposite_pointer_available{};
  std::optional<TargetIdentity> exit_target;

  [[nodiscard]] bool ready() const noexcept;
};

enum class EffectType {
  ShowIdleControl,
  ShowConfirmation,
  HideControl,
  ShowCountdownHud,
  UpdateCountdownHud,
  ShowExitingHud,
  ShowFailureHud,
  HideHud,
  RequestGracefulClose,
  RequestForceClose,
  Diagnostic,
};

struct Effect {
  EffectType type{EffectType::Diagnostic};
  std::optional<TargetIdentity> target;
  std::chrono::milliseconds remaining{};
  double progress{};
  std::string code;
  FlowState from_state{FlowState::NoActiveFlow};
  FlowState to_state{FlowState::NoActiveFlow};

  Effect() = default;
  Effect(EffectType effect_type, std::optional<TargetIdentity> effect_target = {},
         std::chrono::milliseconds effect_remaining = {}, double effect_progress = 0.0,
         std::string effect_code = {}, FlowState effect_from = FlowState::NoActiveFlow,
         FlowState effect_to = FlowState::NoActiveFlow)
      : type(effect_type),
        target(std::move(effect_target)),
        remaining(effect_remaining),
        progress(effect_progress),
        code(std::move(effect_code)),
        from_state(effect_from),
        to_state(effect_to) {}
};

// Runtime adapters may hold resources that are only needed while an exit is
// in progress. The diagnostic transition is also the lifecycle boundary at
// which those resources must be released.
[[nodiscard]] constexpr bool completes_exit_flow(const Effect& effect) noexcept {
  return effect.type == EffectType::Diagnostic && effect.from_state == FlowState::Exiting &&
         effect.to_state == FlowState::NoActiveFlow;
}

using Effects = std::vector<Effect>;

struct FlowSnapshot {
  FlowState state{FlowState::NoActiveFlow};
  LastExitStatus last_exit{LastExitStatus::Never};
  std::optional<TargetIdentity> locked_target;
  std::chrono::milliseconds countdown_remaining{};
  double countdown_progress{};
  bool failure_hud_visible{};
  std::string last_error_code;
};

class FlowStateMachine {
 public:
  explicit FlowStateMachine(std::chrono::seconds control_timeout = std::chrono::seconds{8},
                            std::chrono::seconds exit_grace_period = std::chrono::seconds{5});

  [[nodiscard]] FlowState state() const noexcept {
    return state_;
  }
  [[nodiscard]] FlowSnapshot snapshot(TimePoint now) const;
  void set_control_timeout(std::chrono::seconds timeout);

  [[nodiscard]] Effects on_gesture(TimePoint now, const Prerequisites& prerequisites);
  [[nodiscard]] Effects on_control_clicked(TimePoint now);
  [[nodiscard]] Effects on_dismiss();
  [[nodiscard]] Effects on_confirm(TimePoint now, const TargetIdentity& target,
                                   std::chrono::seconds countdown_duration);
  [[nodiscard]] Effects on_cancel();
  [[nodiscard]] Effects on_prerequisites(TimePoint now, const Prerequisites& prerequisites);
  [[nodiscard]] Effects on_target_status(TimePoint now, TargetStatus status);
  [[nodiscard]] Effects on_graceful_result(TimePoint now, CloseRequestResult result,
                                           std::string error_code = {});
  [[nodiscard]] Effects on_force_result(TimePoint now, CloseRequestResult result,
                                        std::string error_code = {});
  [[nodiscard]] Effects on_overlay_failure(std::string error_code);
  [[nodiscard]] Effects tick(TimePoint now);

 private:
  void transition(FlowState next, Effects& effects, std::string code);
  void reset_flow() noexcept;
  void finish_success(Effects& effects, std::string code);
  void finish_target_unavailable(Effects& effects, std::string code);
  void finish_failure(TimePoint now, Effects& effects, std::string code);
  [[nodiscard]] std::chrono::milliseconds remaining(TimePoint now) const noexcept;

  FlowState state_{FlowState::NoActiveFlow};
  LastExitStatus last_exit_{LastExitStatus::Never};
  Prerequisites prerequisites_{};
  std::optional<TargetIdentity> visible_target_;
  std::optional<TargetIdentity> locked_target_;
  TimePoint control_deadline_{};
  TimePoint countdown_started_{};
  TimePoint countdown_deadline_{};
  TimePoint grace_deadline_{};
  TimePoint force_deadline_{};
  TimePoint failure_hud_deadline_{};
  std::chrono::seconds control_timeout_;
  std::chrono::seconds exit_grace_period_;
  bool force_requested_{};
  bool failure_hud_visible_{};
  std::string last_error_code_;
};

[[nodiscard]] std::string_view to_string(FlowState value) noexcept;
[[nodiscard]] std::string_view to_string(LastExitStatus value) noexcept;

}  // namespace bailout::core
