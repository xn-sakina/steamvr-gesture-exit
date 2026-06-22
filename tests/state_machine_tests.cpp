#include "flow.h"
#include "test_utils.h"

using namespace std::chrono_literals;

namespace {

sbo::ExitTargetIdentity target(std::uint32_t pid, std::string name = "VRChat") {
  return {pid, 12345, "C:/Games/" + name + ".exe", "steam.app." + std::to_string(pid), std::move(name)};
}

sbo::Prerequisites ready(const sbo::ExitTargetIdentity& t) {
  sbo::Prerequisites p;
  p.currentGameAvailable = true;
  p.exitTargetAvailable = true;
  p.configuredHandTracked = true;
  p.oppositePointerAvailable = true;
  p.exitTarget = t;
  return p;
}

void confirm_locks_displayed_target_only() {
  sbo::FlowController flow;
  const auto now = sbo::FlowController::Clock::now();
  const auto displayed = target(42);
  flow.update_prerequisites(ready(displayed), now);
  flow.farewell_gesture(now);
  flow.primary_clicked(now + 10ms);

  const auto changed = target(99, "OtherGame");
  auto result = flow.confirm_clicked(now + 20ms, changed);

  CHECK_EQ(flow.state(), sbo::FlowState::none);
  CHECK_TRUE(!flow.locked_target().has_value());
  CHECK_TRUE(!result.render.hudVisible);
}

void countdown_ignores_new_target_and_settings() {
  sbo::FlowController flow;
  auto settings = sbo::default_settings();
  settings.exitCountdownDurationSeconds = 5;
  const auto now = sbo::FlowController::Clock::now();
  const auto displayed = target(42);
  flow.update_settings(settings, now);
  flow.update_prerequisites(ready(displayed), now);
  flow.farewell_gesture(now);
  flow.primary_clicked(now + 10ms);
  flow.confirm_clicked(now + 20ms, displayed);

  settings.exitCountdownDurationSeconds = 15;
  flow.update_settings(settings, now + 100ms);
  flow.update_prerequisites(ready(target(99, "OtherGame")), now + 100ms);
  auto result = flow.tick(now + 2s);

  CHECK_EQ(flow.state(), sbo::FlowState::countingDown);
  CHECK_TRUE(flow.locked_target().has_value());
  CHECK_EQ(flow.locked_target()->pid, 42u);
  CHECK_TRUE(result.render.secondsRemaining <= 4);
}

void countdown_elapsed_requests_game_exit() {
  sbo::FlowController flow;
  const auto now = sbo::FlowController::Clock::now();
  const auto displayed = target(42);
  flow.update_prerequisites(ready(displayed), now);
  flow.farewell_gesture(now);
  flow.primary_clicked(now + 10ms);
  flow.confirm_clicked(now + 20ms, displayed);
  auto result = flow.tick(now + 6s);

  CHECK_EQ(flow.state(), sbo::FlowState::exiting);
  bool found = false;
  for (const auto& command : result.commands) {
    found = found || command.kind == sbo::CommandKind::requestGracefulClose;
  }
  CHECK_TRUE(found);
  CHECK_TRUE(result.render.hudVisible);
}

void failure_uses_visible_hud_notice() {
  sbo::FlowController flow;
  const auto now = sbo::FlowController::Clock::now();
  const auto displayed = target(42);
  flow.update_prerequisites(ready(displayed), now);
  flow.farewell_gesture(now);
  flow.primary_clicked(now + 10ms);
  flow.confirm_clicked(now + 20ms, displayed);
  flow.tick(now + 6s);
  auto result = flow.force_close_failed(now + 7s, "force_close_failed");

  CHECK_EQ(flow.state(), sbo::FlowState::failureNotice);
  CHECK_EQ(flow.last_exit(), sbo::LastExitStatus::failed);
  CHECK_TRUE(result.render.hudVisible);
  CHECK_TRUE(!result.render.exitControlVisible);
}

void cancel_emits_last_exit_command() {
  sbo::FlowController flow;
  const auto now = sbo::FlowController::Clock::now();
  const auto displayed = target(42);
  flow.update_prerequisites(ready(displayed), now);
  flow.farewell_gesture(now);
  flow.primary_clicked(now + 10ms);
  flow.confirm_clicked(now + 20ms, displayed);

  auto result = flow.cancel_clicked(now + 100ms);

  CHECK_EQ(flow.state(), sbo::FlowState::none);
  CHECK_EQ(flow.last_exit(), sbo::LastExitStatus::canceled);
  bool found = false;
  for (const auto& command : result.commands) {
    found = found || (command.kind == sbo::CommandKind::updateLastExit && command.code == "canceled");
  }
  CHECK_TRUE(found);
}

void target_gone_emits_last_exit_command() {
  sbo::FlowController flow;
  const auto now = sbo::FlowController::Clock::now();
  const auto displayed = target(42);
  flow.update_prerequisites(ready(displayed), now);
  flow.farewell_gesture(now);
  flow.primary_clicked(now + 10ms);
  flow.confirm_clicked(now + 20ms, displayed);

  auto result = flow.confirmed_target_gone_or_invalid(now + 100ms);

  CHECK_EQ(flow.state(), sbo::FlowState::none);
  CHECK_EQ(flow.last_exit(), sbo::LastExitStatus::targetGone);
  bool found = false;
  for (const auto& command : result.commands) {
    found = found || (command.kind == sbo::CommandKind::updateLastExit && command.code == "target_gone");
  }
  CHECK_TRUE(found);
}

void force_success_emits_last_exit_command() {
  sbo::FlowController flow;
  const auto now = sbo::FlowController::Clock::now();
  const auto displayed = target(42);
  flow.update_prerequisites(ready(displayed), now);
  flow.farewell_gesture(now);
  flow.primary_clicked(now + 10ms);
  flow.confirm_clicked(now + 20ms, displayed);
  flow.tick(now + 6s);

  auto result = flow.force_close_succeeded(now + 7s);

  CHECK_EQ(flow.state(), sbo::FlowState::none);
  CHECK_EQ(flow.last_exit(), sbo::LastExitStatus::successForced);
  bool found = false;
  for (const auto& command : result.commands) {
    found = found || (command.kind == sbo::CommandKind::updateLastExit && command.code == "success_forced");
  }
  CHECK_TRUE(found);
}

}  // namespace

void run_state_machine_tests() {
  confirm_locks_displayed_target_only();
  countdown_ignores_new_target_and_settings();
  countdown_elapsed_requests_game_exit();
  failure_uses_visible_hud_notice();
  cancel_emits_last_exit_command();
  target_gone_emits_last_exit_command();
  force_success_emits_last_exit_command();
}

int main() {
  run_state_machine_tests();
  extern void run_gesture_tests();
  extern void run_settings_tests();
  run_gesture_tests();
  run_settings_tests();
  return 0;
}
