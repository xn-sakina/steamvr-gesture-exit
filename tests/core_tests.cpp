#include "core/diagnostics.hpp"
#include "core/gesture_detector.hpp"
#include "core/localization.hpp"
#include "core/settings.hpp"
#include "core/state_machine.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using namespace bailout::core;

namespace {

struct Failure : std::runtime_error { using std::runtime_error::runtime_error; };

#define CHECK(expression) \
  do { if (!(expression)) throw Failure(std::string{"CHECK failed: "} + #expression); } while (false)

TimePoint at(const int milliseconds) { return TimePoint{} + std::chrono::milliseconds{milliseconds}; }

TargetIdentity target(std::uint32_t pid = 42, std::uint64_t token = 7) {
  return {pid, token, "C:\\Games\\Example.exe", "Example Game"};
}

Prerequisites ready(const TargetIdentity& identity = target()) {
  return {true, true, true, identity};
}

bool has_effect(const Effects& effects, const EffectType type) {
  for (const auto& effect : effects) if (effect.type == type) return true;
  return false;
}

GestureSample sample(int milliseconds, Vec3 hand, Vec3 forward = {0, 0, -1},
                     bool tracked = true, std::optional<Vec3> palm = std::nullopt) {
  return {at(milliseconds), tracked, hand, palm.value_or(normalized(hand)), {}, forward};
}

void gesture_recognizes_valid_wave() {
  GestureDetector detector;
  CHECK(!detector.update(sample(0, {0.10, 0.10, -0.50})));
  CHECK(!detector.update(sample(200, {0.30, 0.10, -0.50})));
  CHECK(!detector.update(sample(400, {0.10, 0.10, -0.50})));
  CHECK(detector.update(sample(600, {0.30, 0.10, -0.50})));
  CHECK(!detector.update(sample(800, {0.30, 0.10, -0.50})));
}

void gesture_uses_head_relative_right_axis() {
  GestureDetector detector;
  const Vec3 forward = normalized({1.0, 0.0, -1.0});
  const Vec3 right = normalized({forward.z, 0.0, -forward.x});
  const Vec3 centre{forward.x * 0.55, 0.1, forward.z * 0.55};
  const auto moved = [&](double offset) {
    return Vec3{centre.x + right.x * offset, centre.y, centre.z + right.z * offset};
  };
  CHECK(!detector.update(sample(0, moved(-0.08), forward)));
  CHECK(!detector.update(sample(200, moved(0.08), forward)));
  CHECK(!detector.update(sample(400, moved(-0.08), forward)));
  CHECK(detector.update(sample(600, moved(0.08), forward)));
}

void gesture_rejects_unsafe_or_weak_motion() {
  GestureDetector jitter;
  for (int i = 0; i < 8; ++i) {
    CHECK(!jitter.update(sample(i * 100, {0.20 + (i % 2 ? 0.01 : 0.0), 0.1, -0.5})));
  }
  GestureDetector behind;
  CHECK(!behind.update(sample(0, {0.0, 0.1, 0.5})));
  GestureDetector downward;
  CHECK(!downward.update(sample(0, {0.1, 0.1, -0.5}, {0, 0, -1}, true, Vec3{0, -1, 0})));
  GestureDetector untracked;
  CHECK(!untracked.update(sample(0, {0.1, 0.1, -0.5}, {0, 0, -1}, false)));
  GestureDetector too_slow;
  CHECK(!too_slow.update(sample(0, {0.10, 0.10, -0.50})));
  CHECK(!too_slow.update(sample(500, {0.30, 0.10, -0.50})));
  CHECK(!too_slow.update(sample(1000, {0.10, 0.10, -0.50})));
  CHECK(!too_slow.update(sample(1600, {0.30, 0.10, -0.50})));
}

void settings_round_trip_and_recovery() {
  const auto root = std::filesystem::temp_directory_path() / "bailout-core-settings-test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  SettingsStore store(root / "settings.json");
  auto missing = store.load();
  CHECK(missing.status == SettingsLoadStatus::DefaultsMissing);
  CHECK(missing.settings == Settings::defaults());

  Settings settings;
  settings.configured_hand = Hand::Right;
  settings.countdown_seconds = 15;
  settings.control_timeout_seconds = 12;
  settings.countdown_sound = false;
  settings.language = Language::ChineseSimplified;
  std::string error;
  CHECK(store.save(settings, &error));
  const auto loaded = store.load();
  CHECK(loaded.status == SettingsLoadStatus::Loaded);
  CHECK(loaded.settings == settings);

  settings.countdown_seconds = 2;
  CHECK(!store.save(settings, &error));
  CHECK(error == "SETTINGS_COUNTDOWN_INVALID");

  { std::ofstream output(store.path()); output << "{broken"; }
  CHECK(store.load().status == SettingsLoadStatus::RecoveredCorrupt);
  { std::ofstream output(store.path()); output << R"({"version":99})"; }
  CHECK(store.load().status == SettingsLoadStatus::RecoveredIncompatible);
  std::filesystem::remove_all(root, ec);
}

void localization_resolves_auto_and_manual() {
  CHECK(resolve_language(Language::Auto, "schinese", "en-US") ==
        Language::ChineseSimplified);
  CHECK(resolve_language(Language::Auto, "", "zh-SG") == Language::ChineseSimplified);
  CHECK(resolve_language(Language::Auto, "english", "zh-CN") == Language::English);
  CHECK(resolve_language(Language::English, "schinese", "zh-CN") == Language::English);
  CHECK(localize(StringId::Confirm, Language::English) == "Confirm exit");
  CHECK(localize(StringId::Confirm, Language::ChineseSimplified) == "确认退出");
}

void diagnostics_are_structured_and_privacy_bounded() {
  DiagnosticRecord record;
  record.event = DiagnosticEventType::StateTransition;
  record.code = "FLOW_\"OK\"\n";
  record.from_state = "idle_visible";
  record.to_state = "confirming";
  record.target_name = "Example Game";
  const std::string json = serialize_diagnostic_record(record, "2026-01-01T00:00:00Z");
  CHECK(json.find("\"event\":\"state_transition\"") != std::string::npos);
  CHECK(json.find("FLOW_\\\"OK\\\"\\n") != std::string::npos);
  CHECK(json.find("pose") == std::string::npos);
  CHECK(json.find("chat") == std::string::npos);
  CHECK(json.find("audio") == std::string::npos);
  CHECK(json.find("processes") == std::string::npos);
}

void state_machine_requires_all_prerequisites() {
  const auto identity = target();
  const std::vector<Prerequisites> unavailable{
      {false, true, true, identity}, {true, false, true, identity},
      {true, true, false, identity}, {true, true, true, std::nullopt}};
  for (const auto& prerequisites : unavailable) {
    FlowStateMachine machine;
    CHECK(machine.on_gesture(at(0), prerequisites).empty());
    CHECK(machine.state() == FlowState::NoActiveFlow);
  }
}

void state_machine_happy_graceful_path() {
  FlowStateMachine machine;
  CHECK(has_effect(machine.on_gesture(at(0), ready()), EffectType::ShowIdleControl));
  CHECK(machine.state() == FlowState::IdleVisible);
  CHECK(has_effect(machine.on_control_clicked(at(10)), EffectType::ShowConfirmation));
  CHECK(machine.state() == FlowState::Confirming);
  CHECK(has_effect(machine.on_confirm(at(20), target(), 3s), EffectType::ShowCountdownHud));
  CHECK(machine.snapshot(at(1020)).countdown_remaining == 2000ms);
  CHECK(std::abs(machine.snapshot(at(1020)).countdown_progress - 2.0 / 3.0) < 0.001);
  CHECK(has_effect(machine.tick(at(3020)), EffectType::RequestGracefulClose));
  CHECK(machine.state() == FlowState::Exiting);
  CHECK(machine.on_cancel().empty());
  CHECK(has_effect(machine.on_graceful_result(at(3100), CloseRequestResult::TargetExited),
                   EffectType::HideHud));
  CHECK(machine.state() == FlowState::NoActiveFlow);
  CHECK(machine.snapshot(at(3100)).last_exit == LastExitStatus::Succeeded);
}

void state_machine_timeout_refresh_and_prerequisite_loss() {
  FlowStateMachine machine(2s);
  (void)machine.on_gesture(at(0), ready());
  CHECK(machine.tick(at(1500)).empty());
  // A repeated gesture refreshes only the idle deadline.
  (void)machine.on_gesture(at(1500), ready());
  CHECK(machine.tick(at(3000)).empty());
  CHECK(has_effect(machine.tick(at(3500)), EffectType::HideControl));

  (void)machine.on_gesture(at(4000), ready());
  (void)machine.on_control_clicked(at(4010));
  auto lost = ready();
  lost.opposite_pointer_available = false;
  CHECK(has_effect(machine.on_prerequisites(at(4020), lost), EffectType::HideControl));
  CHECK(machine.state() == FlowState::NoActiveFlow);
}

void state_machine_countdown_rules_and_target_lock() {
  FlowStateMachine machine;
  (void)machine.on_gesture(at(0), ready());
  (void)machine.on_control_clicked(at(1));
  CHECK(machine.on_confirm(at(2), target(43), 5s).empty());
  CHECK(machine.state() == FlowState::Confirming);
  CHECK(machine.on_confirm(at(2), target(), 2s).empty());
  CHECK(machine.on_confirm(at(2), target(), 5s).empty() == false);

  auto tracking_lost = ready();
  tracking_lost.configured_hand_tracked = false;
  tracking_lost.opposite_pointer_available = false;
  CHECK(machine.on_prerequisites(at(100), tracking_lost).empty());
  CHECK(machine.state() == FlowState::CountingDown);
  CHECK(machine.on_gesture(at(200), ready()).empty());
  CHECK(has_effect(machine.on_cancel(), EffectType::HideHud));
  CHECK(machine.snapshot(at(300)).last_exit == LastExitStatus::Cancelled);
}

void state_machine_target_change_is_safe_noop() {
  FlowStateMachine machine;
  (void)machine.on_gesture(at(0), ready());
  (void)machine.on_control_clicked(at(1));
  (void)machine.on_confirm(at(2), target(), 1s);
  CHECK(has_effect(machine.on_target_status(at(500), TargetStatus::IdentityMismatch),
                   EffectType::HideHud));
  CHECK(machine.state() == FlowState::NoActiveFlow);
  CHECK(machine.snapshot(at(500)).last_exit == LastExitStatus::TargetUnavailable);
  CHECK(!has_effect(machine.tick(at(2000)), EffectType::RequestGracefulClose));
}

void state_machine_force_fallback_and_failure_hud() {
  FlowStateMachine machine(8s, 5s);
  (void)machine.on_gesture(at(0), ready());
  (void)machine.on_control_clicked(at(1));
  (void)machine.on_confirm(at(2), target(), 1s);
  (void)machine.tick(at(1002));
  CHECK(machine.state() == FlowState::Exiting);
  CHECK(!has_effect(machine.tick(at(6001)), EffectType::RequestForceClose));
  CHECK(has_effect(machine.tick(at(6002)), EffectType::RequestForceClose));
  CHECK(!has_effect(machine.tick(at(7000)), EffectType::RequestForceClose));
  CHECK(has_effect(machine.on_force_result(at(7000), CloseRequestResult::Failed, "ACCESS_DENIED"),
                   EffectType::ShowFailureHud));
  auto snapshot = machine.snapshot(at(7000));
  CHECK(snapshot.state == FlowState::NoActiveFlow);
  CHECK(snapshot.last_exit == LastExitStatus::Failed);
  CHECK(snapshot.failure_hud_visible);
  CHECK(snapshot.last_error_code == "ACCESS_DENIED");
  CHECK(!has_effect(machine.tick(at(10999)), EffectType::HideHud));
  CHECK(has_effect(machine.tick(at(11000)), EffectType::HideHud));
}

}  // namespace

int main() {
  const std::vector<std::pair<std::string, std::function<void()>>> tests{
      {"gesture recognizes valid wave", gesture_recognizes_valid_wave},
      {"gesture uses head relative right axis", gesture_uses_head_relative_right_axis},
      {"gesture rejects unsafe or weak motion", gesture_rejects_unsafe_or_weak_motion},
      {"settings round trip and recovery", settings_round_trip_and_recovery},
      {"localization resolves auto and manual", localization_resolves_auto_and_manual},
      {"diagnostics structured privacy", diagnostics_are_structured_and_privacy_bounded},
      {"state requires prerequisites", state_machine_requires_all_prerequisites},
      {"state happy graceful path", state_machine_happy_graceful_path},
      {"state timeout and loss", state_machine_timeout_refresh_and_prerequisite_loss},
      {"state countdown rules", state_machine_countdown_rules_and_target_lock},
      {"state target change", state_machine_target_change_is_safe_noop},
      {"state force failure", state_machine_force_fallback_and_failure_hud},
  };
  int failures = 0;
  for (const auto& [name, test] : tests) {
    try {
      test();
      std::cout << "PASS " << name << '\n';
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "FAIL " << name << ": " << error.what() << '\n';
    }
  }
  std::cout << tests.size() - failures << '/' << tests.size() << " tests passed\n";
  return failures == 0 ? 0 : 1;
}
