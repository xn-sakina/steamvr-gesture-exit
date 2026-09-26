#include "core/diagnostics.hpp"
#include "core/dashboard_ui.hpp"
#include "core/gesture_detector.hpp"
#include "core/localization.hpp"
#include "core/settings.hpp"
#include "core/settings_persistence.hpp"
#include "core/state_machine.hpp"
#include "core/vr_game_classifier.hpp"
#include "core/wrist_pose_smoother.hpp"
#include "app/runtime_interfaces.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;
using namespace bailout::core;

namespace {

struct Failure : std::runtime_error {
  using std::runtime_error::runtime_error;
};

#define CHECK(expression)                                         \
  do {                                                            \
    if (!(expression))                                            \
      throw Failure(std::string{"CHECK failed: "} + #expression); \
  } while (false)

TimePoint at(const int milliseconds) {
  return TimePoint{} + std::chrono::milliseconds{milliseconds};
}

TargetIdentity target(std::uint32_t pid = 42, std::uint64_t token = 7) {
  return {pid, token, "C:\\Games\\Example.exe", "Example Game"};
}

Prerequisites ready(const TargetIdentity& identity = target()) {
  return {true, true, true, true, true, identity};
}

bool has_effect(const Effects& effects, const EffectType type) {
  for (const auto& effect : effects)
    if (effect.type == type)
      return true;
  return false;
}

bool completes_exit(const Effects& effects) {
  return std::any_of(effects.begin(), effects.end(), completes_exit_flow);
}

GestureSample sample(int milliseconds, Vec3 hand, Vec3 forward = {0, 0, -1}, bool tracked = true,
                     Vec3 headset = {}, bool input_active = false) {
  return {at(milliseconds), tracked, hand, headset, forward, input_active};
}

void gesture_recognizes_valid_wave() {
  GestureDetector detector;
  const Vec3 headset{0.0, 1.65, 0.0};
  const auto upper_body_sample = [&](int milliseconds, double x) {
    return sample(milliseconds, {x, 1.20, -0.50}, {0, 0, -1}, true, headset);
  };
  CHECK(!detector.update(upper_body_sample(0, 0.10)));
  CHECK(!detector.update(upper_body_sample(250, 0.30)));
  CHECK(!detector.update(upper_body_sample(500, 0.10)));
  CHECK(!detector.update(upper_body_sample(750, 0.30)));
  CHECK(detector.update(upper_body_sample(1000, 0.10)));
  CHECK(!detector.update(upper_body_sample(1200, 0.10)));
}

void gesture_recognizes_compact_chest_wave() {
  GestureDetector detector;
  const Vec3 headset{0.0, 1.65, 0.0};
  const auto chest_sample = [&](int milliseconds, double x) {
    return sample(milliseconds, {x, 1.18, -0.48}, {0, 0, -1}, true, headset);
  };
  CHECK(!detector.update(chest_sample(0, 0.05)));
  CHECK(!detector.update(chest_sample(220, 0.19)));
  CHECK(!detector.update(chest_sample(440, 0.05)));
  CHECK(!detector.update(chest_sample(660, 0.19)));
  CHECK(detector.update(chest_sample(880, 0.05)));
}

void gesture_recognizes_center_crossing_wave() {
  GestureDetector detector;
  const auto waved = [&](int milliseconds, double x) {
    return sample(milliseconds, {x, 0.10, -0.50});
  };
  CHECK(!detector.update(waved(0, -0.10)));
  CHECK(!detector.update(waved(250, 0.10)));
  CHECK(!detector.update(waved(500, -0.10)));
  CHECK(!detector.update(waved(750, 0.10)));
  CHECK(detector.update(waved(1000, -0.10)));
}

void gesture_recognizes_sampled_controller_wave() {
  GestureDetector detector;
  bool recognized = false;
  for (int frame = 0; frame < 180; ++frame) {
    const double seconds = static_cast<double>(frame) / 90.0;
    const double x = 0.15 + 0.12 * std::sin(2.0 * 3.141592653589793 * 1.2 * seconds);
    if (detector.update(sample(static_cast<int>(seconds * 1000.0), {x, 1.08, -0.55}, {0, 0, -1},
                               true, {0, 1.65, 0}))) {
      recognized = true;
      break;
    }
  }
  CHECK(recognized);
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
  CHECK(!detector.update(sample(250, moved(0.12), forward)));
  CHECK(!detector.update(sample(500, moved(-0.08), forward)));
  CHECK(!detector.update(sample(750, moved(0.12), forward)));
  CHECK(detector.update(sample(1000, moved(-0.08), forward)));
}

void gesture_rejects_unsafe_or_weak_motion() {
  GestureDetector jitter;
  for (int i = 0; i < 8; ++i) {
    CHECK(!jitter.update(sample(i * 100, {0.20 + (i % 2 ? 0.01 : 0.0), 0.1, -0.5})));
  }
  CHECK(jitter.diagnostic().decision == GestureDecision::InsufficientStroke);
  CHECK(jitter.diagnostic().amplitude_m < jitter.config().minimum_stroke_metres);
  GestureDetector behind;
  for (int i = 0; i < 4; ++i)
    CHECK(!behind.update(sample(i * 400, {i % 2 ? 0.3 : 0.1, 0.1, 0.5})));
  CHECK(behind.diagnostic().decision == GestureDecision::BehindHead);
  GestureDetector untracked;
  CHECK(!untracked.update(sample(0, {0.1, 0.1, -0.5}, {0, 0, -1}, false)));
  CHECK(untracked.diagnostic().decision == GestureDecision::HandUntracked);
}

void gesture_accepts_real_seated_postures() {
  const Vec3 headset{0.0, 1.55, 0.0};
  for (const double height : {0.72, 1.02}) {
    GestureDetector detector;
    const std::array<double, 5> x{0.04, 0.24, 0.04, 0.24, 0.04};
    for (std::size_t i = 0; i + 1 < x.size(); ++i) {
      CHECK(!detector.update(
          sample(static_cast<int>(i) * 250, {x[i], height, -0.48}, {0, 0, -1}, true, headset)));
    }
    CHECK(detector.update(sample(1000, {x.back(), height, -0.48}, {0, 0, -1}, true, headset)));
  }
}

void gesture_recognizes_lazy_fast_low_wave() {
  for (const double height : {0.68, 0.96}) {
    GestureDetector detector;
    bool recognized = false;
    // A compact 10 cm peak-to-peak wave at 3 Hz is intentionally much faster
    // and lower than the old idealized chest-level gesture.
    for (int milliseconds = 0; milliseconds <= 1200; milliseconds += 10) {
      const double seconds = static_cast<double>(milliseconds) / 1000.0;
      const double x = 0.12 + 0.05 * std::sin(2.0 * 3.141592653589793 * 3.0 * seconds);
      if (detector.update(
              sample(milliseconds, {x, height, -0.48}, {0, 0, -1}, true, {0, 1.55, 0}))) {
        recognized = true;
        break;
      }
    }
    CHECK(recognized);
    CHECK(detector.diagnostic().span_ms < 700);
  }
}

void gesture_rejects_game_input_and_body_motion() {
  GestureDetector active_input;
  for (int milliseconds = 0; milliseconds <= 1200; milliseconds += 10) {
    const double seconds = static_cast<double>(milliseconds) / 1000.0;
    const double x = 0.12 + 0.07 * std::sin(2.0 * 3.141592653589793 * 2.5 * seconds);
    CHECK(!active_input.update(sample(milliseconds, {x, 0.85, -0.48}, {0, 0, -1}, true, {}, true)));
  }
  CHECK(active_input.diagnostic().decision == GestureDecision::InputActive);

  GestureDetector body_motion;
  for (int milliseconds = 0; milliseconds <= 1200; milliseconds += 10) {
    const double seconds = static_cast<double>(milliseconds) / 1000.0;
    const double body_x = 0.09 * std::sin(2.0 * 3.141592653589793 * 2.5 * seconds);
    const Vec3 headset{body_x, 1.55, 0.0};
    CHECK(!body_motion.update(
        sample(milliseconds, {body_x + 0.12, 0.85, -0.48}, {0, 0, -1}, true, headset)));
  }
  CHECK(body_motion.diagnostic().decision == GestureDecision::InsufficientStroke);
}

void gesture_rejects_unstable_and_abrupt_motion() {
  GestureDetector wandering;
  for (const auto [time, x] : std::array<std::pair<int, double>, 5>{
           {{0, 0.04}, {180, 0.18}, {360, 0.04}, {540, 0.30}, {720, 0.16}}})
    CHECK(!wandering.update(sample(time, {x, 0.85, -0.48})));
  CHECK(wandering.diagnostic().decision == GestureDecision::UnstablePath ||
        wandering.diagnostic().decision == GestureDecision::IrregularMotion);

  GestureDetector abrupt;
  const std::array<double, 5> extrema{0.12, 0.34, -0.10, 0.34, -0.10};
  int milliseconds = 0;
  CHECK(!abrupt.update(sample(milliseconds, {extrema.front(), 0.85, -0.48})));
  for (std::size_t stroke = 1; stroke < extrema.size(); ++stroke) {
    const double direction = extrema[stroke] > extrema[stroke - 1] ? 1.0 : -1.0;
    milliseconds += 50;
    CHECK(!abrupt.update(
        sample(milliseconds, {extrema[stroke - 1] + direction * 0.02, 0.85, -0.48})));
    milliseconds += 10;
    CHECK(!abrupt.update(sample(milliseconds, {extrema[stroke] - direction * 0.02, 0.85, -0.48})));
    milliseconds += 50;
    CHECK(!abrupt.update(sample(milliseconds, {extrema[stroke], 0.85, -0.48})));
  }
  CHECK(abrupt.diagnostic().decision == GestureDecision::AbruptMotion ||
        abrupt.diagnostic().decision == GestureDecision::StrokeTooFast);
}

void gesture_requires_deliberate_repeated_lateral_strokes() {
  GestureDetector two_reversals;
  for (const auto [time, x] :
       std::array<std::pair<int, double>, 4>{{{0, 0.0}, {300, 0.20}, {600, 0.0}, {900, 0.20}}})
    CHECK(!two_reversals.update(sample(time, {x, 0.1, -0.5})));
  CHECK(!two_reversals.update(sample(1000, {0.20, 0.1, -0.5})));
  CHECK(two_reversals.diagnostic().decision == GestureDecision::InsufficientReversals);

  GestureDetector too_fast;
  for (const auto [time, x] : std::array<std::pair<int, double>, 5>{
           {{0, 0.0}, {60, 0.20}, {120, 0.0}, {180, 0.20}, {240, 0.0}}})
    CHECK(!too_fast.update(sample(time, {x, 0.1, -0.5})));
  CHECK(too_fast.diagnostic().decision == GestureDecision::StrokeTooFast);

  GestureDetector non_lateral;
  for (const auto [time, point] : std::array<std::pair<int, Vec3>, 5>{{
           {0, {0.0, 0.0, -0.5}},
           {250, {0.20, 0.22, -0.5}},
           {500, {0.0, 0.0, -0.5}},
           {750, {0.20, 0.22, -0.5}},
           {1000, {0.0, 0.0, -0.5}},
       }})
    CHECK(!non_lateral.update(sample(time, point)));
  CHECK(non_lateral.diagnostic().decision == GestureDecision::NonLateralMotion);

  GestureDetector paused;
  for (const auto [time, x] : std::array<std::pair<int, double>, 5>{
           {{0, 0.0}, {200, 0.20}, {1100, 0.0}, {1400, 0.20}, {1700, 0.0}}})
    CHECK(!paused.update(sample(time, {x, 0.1, -0.5})));
  CHECK(paused.diagnostic().decision == GestureDecision::StrokeTooSlow);

  GestureDetector irregular;
  for (const auto [time, x] : std::array<std::pair<int, double>, 5>{
           {{0, 0.0}, {200, 0.14}, {350, 0.0}, {850, 0.14}, {1000, 0.0}}})
    CHECK(!irregular.update(sample(time, {x, 0.1, -0.5})));
  CHECK(irregular.diagnostic().decision == GestureDecision::IrregularMotion);

  GestureDetector stationary_prelude;
  CHECK(!stationary_prelude.update(sample(0, {0.0, 0.1, -0.5})));
  CHECK(!stationary_prelude.update(sample(500, {0.0, 0.1, -0.5})));
  CHECK(!stationary_prelude.update(sample(1000, {0.0, 0.1, -0.5})));
  CHECK(!stationary_prelude.update(sample(1200, {0.20, 0.1, -0.5})));
  CHECK(!stationary_prelude.update(sample(1450, {0.0, 0.1, -0.5})));
  CHECK(!stationary_prelude.update(sample(1700, {0.20, 0.1, -0.5})));
  CHECK(stationary_prelude.update(sample(1950, {0.0, 0.1, -0.5})));
}

void settings_round_trip_and_recovery() {
  const auto root = std::filesystem::temp_directory_path() / "bailout-core-settings-test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  SettingsStore store(root / "settings.json");
  auto missing = store.load();
  CHECK(missing.status == SettingsLoadStatus::DefaultsMissing);
  CHECK(missing.settings == Settings::defaults());
  CHECK(missing.diagnostic_code == "SETTINGS_MISSING");

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

  auto stale_temporary = store.path();
  stale_temporary += ".tmp";
  {
    std::ofstream output(stale_temporary);
    output << "{interrupted";
  }
  CHECK(store.load().settings == settings);
  CHECK(!std::filesystem::exists(stale_temporary));

  // A failed write must leave the last committed settings readable.
  auto blocked_temporary = store.path();
  blocked_temporary += ".tmp";
  std::filesystem::create_directory(blocked_temporary);
  auto changed = settings;
  changed.countdown_seconds = 10;
  CHECK(!store.save(changed, &error));
  CHECK(error == "SETTINGS_WRITE_FAILED");
  CHECK(store.load().settings == settings);
  std::filesystem::remove(blocked_temporary, ec);

  settings.countdown_seconds = 2;
  CHECK(!store.save(settings, &error));
  CHECK(error == "SETTINGS_COUNTDOWN_INVALID");

  {
    std::ofstream output(store.path());
    output << "{broken";
  }
  CHECK(store.load().status == SettingsLoadStatus::RecoveredCorrupt);
  {
    std::ofstream output(store.path());
    output << R"({"version":99})";
  }
  CHECK(store.load().status == SettingsLoadStatus::RecoveredIncompatible);
  const std::string valid =
      R"({"version":1,"configuredHand":"right","countdownSeconds":15,"controlTimeoutSeconds":12,"countdownSound":false,"language":"zh-CN"})";
  const std::vector<std::string> invalid_documents{
      "garbage " + valid,
      valid + " garbage",
      valid + " {}",
      valid.substr(0, valid.size() - 1) + R"(,"version":1})",
      valid.substr(0, valid.size() - 1) + ",}",
      valid.substr(0, valid.size() - 1) + ",\n }",
      "/* comment */ " + valid,
      valid + "\v",
      "[" + valid + "]",
      R"({"wrapper":)" + valid + "}",
      R"({"version":1,"configuredHand":"right","countdownSeconds":"15","controlTimeoutSeconds":12,"countdownSound":false,"language":"zh-CN"})",
      valid.substr(0, valid.size() - 1) + R"(,"unknown":true})",
  };
  for (const auto& document : invalid_documents) {
    {
      std::ofstream output(store.path());
      output << document;
    }
    const auto result = store.load();
    if (result.status != SettingsLoadStatus::RecoveredCorrupt) {
      throw Failure("invalid settings document accepted: " + document);
    }
    CHECK(result.settings == Settings::defaults());
    CHECK(result.diagnostic_code == "SETTINGS_JSON_INVALID");
  }
  {
    std::ofstream output(store.path());
    output << " \n " << valid << " \n ";
  }
  CHECK(store.load().settings == loaded.settings);
  std::filesystem::remove_all(root, ec);
}

void settings_worker_reports_durable_results_in_order() {
  const auto root = std::filesystem::temp_directory_path() / "bailout-settings-commits-test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  const auto path = root / "settings.json";
  Settings first = Settings::defaults();
  first.countdown_seconds = 10;
  Settings second = first;
  second.language = Language::ChineseSimplified;
  Settings third = second;
  third.control_timeout_seconds = 12;
  SettingsPersistenceWorker worker(path);
  CHECK(worker.enqueue({first, "countdown_seconds", "10"}));
  CHECK(worker.enqueue({second, "language", "zh-CN"}));
  CHECK(worker.enqueue({third, "control_timeout_seconds", "12"}));
  worker.stop();
  auto results = worker.take_results();
  CHECK(results.size() == 3);
  CHECK(results[0].saved && results[0].change.settings == first);
  CHECK(results[1].saved && results[1].change.settings == second);
  CHECK(results[2].saved && results[2].change.settings == third);
  CHECK(SettingsStore(path).load().settings == third);
  CHECK(worker.take_results().empty());
  CHECK(!worker.enqueue({first, "countdown_seconds", "10"}));
  worker.stop();
  std::filesystem::remove_all(root, ec);
}

void settings_worker_failure_preserves_last_commit_and_bounds_queue() {
  const auto root = std::filesystem::temp_directory_path() / "bailout-settings-failure-test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  const auto path = root / "settings.json";
  SettingsStore store(path);
  const Settings committed = Settings::defaults();
  CHECK(store.save(committed));
  auto temporary = path;
  temporary += ".tmp";
  std::filesystem::create_directory(temporary);
  Settings changed = committed;
  changed.countdown_seconds = 15;
  {
    SettingsPersistenceWorker worker(path);
    for (int i = 0; i < 64; ++i)
      CHECK(worker.enqueue({changed, "countdown_seconds", "15"}));
    CHECK(!worker.enqueue({changed, "countdown_seconds", "15"}));
    worker.stop();
    auto results = worker.take_results();
    CHECK(results.size() == 64);
    for (const auto& result : results) {
      CHECK(!result.saved);
      CHECK(result.error == "SETTINGS_WRITE_FAILED");
    }
  }
  CHECK(store.load().settings == committed);
  std::filesystem::remove(temporary, ec);
  {
    SettingsPersistenceWorker worker(path);
    CHECK(worker.enqueue({changed, "countdown_seconds", "15"}));
    worker.stop();
    auto results = worker.take_results();
    CHECK(results.size() == 1 && results.front().saved);
  }
  CHECK(store.load().settings == changed);
  std::filesystem::remove_all(root, ec);
}

void settings_worker_concurrent_stop_joins_once() {
  const auto root = std::filesystem::temp_directory_path() / "bailout-settings-stop-test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  const auto path = root / "settings.json";
  SettingsPersistenceWorker worker(path);
  CHECK(worker.enqueue({Settings::defaults(), "language", "auto"}));
  std::thread first([&] { worker.stop(); });
  std::thread second([&] { worker.stop(); });
  first.join();
  second.join();
  const auto results = worker.take_results();
  CHECK(results.size() == 1 && results.front().saved);
  CHECK(SettingsStore(path).load().settings == Settings::defaults());
  std::filesystem::remove_all(root, ec);
}

void localization_resolves_auto_and_manual() {
  CHECK(resolve_language(Language::Auto, "schinese", "en-US") == Language::English);
  CHECK(resolve_language(Language::Auto, "", "zh-SG") == Language::ChineseSimplified);
  CHECK(resolve_language(Language::Auto, "english", "zh-CN") == Language::ChineseSimplified);
  CHECK(resolve_language(Language::Auto, "schinese", "") == Language::ChineseSimplified);
  CHECK(resolve_language(Language::English, "schinese", "zh-CN") == Language::English);
  CHECK(localize(StringId::Confirm, Language::English) == "Confirm exit");
  CHECK(localize(StringId::Confirm, Language::ChineseSimplified) == "确认退出");
  CHECK(localize(StringId::ConfirmTitle, Language::ChineseSimplified) == "是否退出？");
  CHECK(localize(StringId::ExitGame, Language::ChineseSimplified) == "退出");
  CHECK(localize(StringId::Dismiss, Language::ChineseSimplified) == "取消");
  CHECK(localize(StringId::HandTracking, Language::ChineseSimplified) == "挥手手柄");
  CHECK(localize(StringId::OppositePointer, Language::ChineseSimplified) == "另一只手柄");
  CHECK(localize(StringId::ControllerTracked, Language::ChineseSimplified) == "已识别到手柄");
  CHECK(localize(StringId::ClickHandReady, Language::ChineseSimplified) ==
        "退出按钮出现后，指向按钮并扣动扳机。");
  CHECK(localize(StringId::SettingsSaveFailed, Language::ChineseSimplified) ==
        "设置保存失败，请重试。");
  CHECK(localize(StringId::AppName, Language::ChineseSimplified) == "SteamVR手势退出");
  CHECK(localize(StringId::AppName, Language::English) == "SteamVR Gesture Exit");
  CHECK(localize(StringId::ThumbnailTitle, Language::ChineseSimplified) == "SteamVR手势退出");
  CHECK(localize(StringId::ThumbnailDescription, Language::ChineseSimplified) ==
        "挥手唤出退出按钮");
  CHECK(localize(StringId::SecondsUnit, Language::ChineseSimplified) == "秒");
  CHECK(localize(StringId::EnglishLanguage, Language::ChineseSimplified) == "English");
  CHECK(localize_last_exit(LastExitStatus::Never, Language::ChineseSimplified) == "从未");
  CHECK(localize_last_exit(LastExitStatus::Succeeded, Language::ChineseSimplified) == "成功");
  CHECK(localize_last_exit(LastExitStatus::Cancelled, Language::ChineseSimplified) == "已取消");
  CHECK(localize_last_exit(LastExitStatus::Failed, Language::ChineseSimplified) == "失败");
  CHECK(localize_last_exit(LastExitStatus::TargetUnavailable, Language::ChineseSimplified) ==
        "游戏已关闭或切换");
  CHECK(localize_last_exit(LastExitStatus::Succeeded, Language::English) == "Succeeded");
}

void diagnostics_are_structured_and_privacy_bounded() {
  DiagnosticRecord record;
  record.event = DiagnosticEventType::StateTransition;
  record.code = "FLOW_\"OK\"\n";
  record.from_state = "idle_visible";
  record.to_state = "confirming";
  record.current_game_status = "AVAILABLE";
  record.exit_target_status = "PROCESS_ACCESS_DENIED";
  record.target_name = "Example Game";
  record.debug_only = true;
  record.details["sample_count"] = "4";
  record.details["reason"] = "quoted \"value\"";
  const std::string json = serialize_diagnostic_record(record, "2026-01-01T00:00:00Z");
  CHECK(json.find("\"event\":\"state_transition\"") != std::string::npos);
  CHECK(json.find("\"level\":\"debug\"") != std::string::npos);
  CHECK(json.find("\"sample_count\":\"4\"") != std::string::npos);
  CHECK(json.find("\"reason\":\"quoted \\\"value\\\"\"") != std::string::npos);
  CHECK(json.find("FLOW_\\\"OK\\\"\\n") != std::string::npos);
  CHECK(json.find("\"current_game_status\":\"AVAILABLE\"") != std::string::npos);
  CHECK(json.find("\"exit_target_status\":\"PROCESS_ACCESS_DENIED\"") != std::string::npos);
  CHECK(json.find("pose") == std::string::npos);
  CHECK(json.find("chat") == std::string::npos);
  CHECK(json.find("audio") == std::string::npos);
  CHECK(json.find("processes") == std::string::npos);
}

void diagnostics_debug_records_follow_build_type() {
  const auto path = std::filesystem::temp_directory_path() / "bailout-debug-filter.jsonl";
  std::error_code ec;
  std::filesystem::remove(path, ec);
  DiagnosticLog log(path);
  DiagnosticRecord normal{DiagnosticEventType::Lifecycle, "NORMAL"};
  DiagnosticRecord detailed{DiagnosticEventType::StateTransition, "DEBUG_ONLY"};
  detailed.debug_only = true;
  CHECK(log.append(normal));
  CHECK(log.append(detailed));
  std::ifstream input(path);
  std::string first, second;
  CHECK(static_cast<bool>(std::getline(input, first)));
  CHECK(first.find("NORMAL") != std::string::npos);
#ifdef NDEBUG
  CHECK(!static_cast<bool>(std::getline(input, second)));
#else
  CHECK(static_cast<bool>(std::getline(input, second)));
  CHECK(second.find("DEBUG_ONLY") != std::string::npos);
  CHECK(!static_cast<bool>(std::getline(input, second)));
#endif
  std::filesystem::remove(path, ec);
}

void diagnostics_async_queue_drains_in_order() {
  const auto path = std::filesystem::temp_directory_path() / "bailout-async-log.jsonl";
  std::error_code ec;
  std::filesystem::remove(path, ec);
  DiagnosticLog log(path);
  log.start_async();
  log.start_async();
  for (int i = 0; i < 100; ++i)
    CHECK(log.append({DiagnosticEventType::Lifecycle, "ASYNC_" + std::to_string(i)}));
  std::string error;
  CHECK(log.append({DiagnosticEventType::Lifecycle, "BARRIER"}, &error));
  log.stop_async();
  log.stop_async();
  CHECK(log.append({DiagnosticEventType::Lifecycle, "AFTER_STOP"}));

  log.start_async();
  std::atomic_bool producer_ok = true;
  std::thread producer([&] {
    for (int i = 0; i < 200; ++i) {
      if (!log.append({DiagnosticEventType::Lifecycle, "RACE_" + std::to_string(i)}))
        producer_ok = false;
    }
  });
  log.stop_async();
  producer.join();
  CHECK(producer_ok);

  std::ifstream input(path);
  std::string line;
  for (int i = 0; i < 100; ++i) {
    CHECK(static_cast<bool>(std::getline(input, line)));
    CHECK(line.find("\"code\":\"ASYNC_" + std::to_string(i) + "\"") != std::string::npos);
  }
  CHECK(static_cast<bool>(std::getline(input, line)));
  CHECK(line.find("\"code\":\"BARRIER\"") != std::string::npos);
  CHECK(static_cast<bool>(std::getline(input, line)));
  CHECK(line.find("\"code\":\"AFTER_STOP\"") != std::string::npos);
  for (int i = 0; i < 200; ++i) {
    CHECK(static_cast<bool>(std::getline(input, line)));
    CHECK(line.find("\"code\":\"RACE_" + std::to_string(i) + "\"") != std::string::npos);
  }
  CHECK(!static_cast<bool>(std::getline(input, line)));
  input.close();
  std::filesystem::remove(path, ec);
}

void diagnostics_recover_interrupted_jsonl_tail() {
  const auto path = std::filesystem::temp_directory_path() / "bailout-interrupted-log.jsonl";
  std::error_code ec;
  std::filesystem::remove(path, ec);
  {
    std::ofstream output(path, std::ios::binary);
    output << "{\"code\":\"COMPLETE\"}\n{\"code\":\"PARTIAL";
  }
  DiagnosticLog log(path);
  CHECK(log.append({DiagnosticEventType::Lifecycle, "AFTER_RESTART"}));
  std::ifstream input(path);
  std::string first, second;
  CHECK(static_cast<bool>(std::getline(input, first)));
  CHECK(first == "{\"code\":\"COMPLETE\"}");
  CHECK(static_cast<bool>(std::getline(input, second)));
  CHECK(second.find("AFTER_RESTART") != std::string::npos);
  CHECK(!static_cast<bool>(std::getline(input, second)));
  input.close();
  std::filesystem::remove(path, ec);
}

void diagnostics_destructor_joins_writer() {
  const auto path = std::filesystem::temp_directory_path() / "bailout-log-destructor.jsonl";
  std::error_code ec;
  std::filesystem::remove(path, ec);
  {
    DiagnosticLog log(path);
    log.start_async();
    for (int i = 0; i < 100; ++i)
      CHECK(log.append({DiagnosticEventType::Lifecycle, "DRAIN_" + std::to_string(i)}));
  }
  std::ifstream input(path);
  std::string line;
  int count = 0;
  while (std::getline(input, line))
    ++count;
  CHECK(count == 100);
  input.close();
  std::filesystem::remove(path, ec);
}

void diagnostics_rotate_at_a_fixed_size() {
  const auto root = std::filesystem::temp_directory_path() / "bailout-core-log-test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root);
  const auto path = root / "runtime.log";
  auto backup = path;
  backup += ".1";
  DiagnosticLog log(path);
  std::string error;
  DiagnosticRecord record{};
  record.event = DiagnosticEventType::Lifecycle;
  record.code = "FIRST";
  CHECK(log.append(record, &error));
  CHECK(!std::filesystem::exists(backup));
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << std::string(4 * 1024 * 1024 - 8, 'x');
  }
  record.code = "SECOND";
  CHECK(log.append(record, &error));
  CHECK(std::filesystem::file_size(backup) == 4 * 1024 * 1024 - 8);
  CHECK(std::filesystem::file_size(path) < 4 * 1024 * 1024);
  {
    std::ifstream input(path);
    std::string line;
    CHECK(static_cast<bool>(std::getline(input, line)));
    CHECK(line.find("SECOND") != std::string::npos);
    CHECK(!static_cast<bool>(std::getline(input, line)));
  }
  record.code = "THIRD";
  CHECK(log.append(record, &error));
  CHECK(std::filesystem::file_size(backup) == 4 * 1024 * 1024 - 8);
  CHECK(std::filesystem::file_size(path) < 4 * 1024 * 1024);
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << std::string(4 * 1024 * 1024 + 8, 'x');
  }
  record.code = "AFTER_LEGACY_LOG";
  CHECK(log.append(record, &error));
  CHECK(std::filesystem::file_size(path) < 4 * 1024 * 1024);
  CHECK(std::filesystem::file_size(backup) == 4 * 1024 * 1024 - 8);
  std::filesystem::remove_all(root, ec);
}

void vr_game_classifier_accepts_games_and_rejects_runtime_apps() {
  CHECK(is_current_vr_game_candidate({"steam.app.438100", "url", false, false, false, false}));
  CHECK(is_current_vr_game_candidate({"steam.app.450390", "binary", false, false, false, false}));
  CHECK(is_current_vr_game_candidate({"steam.app.620980", "url", false, false, false, false}));
  CHECK(is_current_vr_game_candidate({"steam.app.438100", "", false, false, false, false}));

  CHECK(
      !is_current_vr_game_candidate({"com.example.vr-tool", "binary", false, false, false, false}));
  CHECK(!is_current_vr_game_candidate(
      {"com.example.indie-vr-game", "binary", false, false, false, false}));
  CHECK(!is_current_vr_game_candidate({"steam.app.999999", "url", false, false, false, false}));

  CHECK(
      !is_current_vr_game_candidate({"steam.app.not-a-number", "url", false, false, false, false}));
  CHECK(!is_current_vr_game_candidate(
      {"openvr.tool.steamvr_environments", "binary", false, false, false, false}));
  CHECK(!is_current_vr_game_candidate(
      {"steam.overlay.1009850", "binary", false, false, false, false}));
  CHECK(!is_current_vr_game_candidate(
      {"system.generated.unknown.exe", "binary", false, false, false, false}));
  CHECK(!is_current_vr_game_candidate({"steam.app.438100", "binary", true, false, false, false}));
  CHECK(!is_current_vr_game_candidate({"steam.app.438100", "binary", false, true, false, false}));
  CHECK(!is_current_vr_game_candidate({"steam.app.438100", "binary", false, false, true, false}));
  CHECK(!is_current_vr_game_candidate({"steam.app.438100", "binary", false, false, false, true}));
  CHECK(!is_current_vr_game_candidate({"steam.app.438100", "unknown", false, false, false, false}));
}

void pointer_availability_uses_tracked_controller() {
  bailout::app::HandPose hand;
  hand.connected = true;
  hand.tracked = true;
  CHECK(!hand.pointer_available());
  hand.device_index = 7;
  CHECK(hand.pointer_available());
  CHECK(!hand.trigger_available);
  hand.trigger_available = true;
  hand.tracked = false;
  CHECK(!hand.pointer_available());
  hand.tracked = true;
  hand.connected = false;
  CHECK(!hand.pointer_available());
  hand.connected = true;
  CHECK(hand.pointer_available());
}

void wrist_pose_smoothing_follows_and_resets_safely() {
  WristPoseSmoother smoother;
  const WristPose origin{{0, 1, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  const WristPose moved{{0.1, 1, 0}, {0, 0, -1}, {0, 1, 0}, {1, 0, 0}};
  CHECK(std::abs(smoother.update(origin, at(0)).position.x) < 1e-9);
  const auto filtered = smoother.update(moved, at(75));
  CHECK(filtered.position.x > 0.05 && filtered.position.x < 0.1);
  CHECK(std::abs(length(filtered.right) - 1.0) < 1e-9);
  CHECK(std::abs(length(filtered.up) - 1.0) < 1e-9);
  CHECK(std::abs(length(filtered.back) - 1.0) < 1e-9);
  CHECK(std::abs(dot(filtered.right, filtered.up)) < 1e-9);
  CHECK(smoother.update(moved, at(150)).position.x > filtered.position.x);
  CHECK(std::abs(smoother.update(origin, at(500)).position.x) < 1e-9);
  CHECK(std::abs(
            smoother.update({{2, 1, 0}, origin.right, origin.up, origin.back}, at(510)).position.x -
            2.0) < 1e-9);
  smoother.reset();
  CHECK(std::abs(smoother.update(origin, at(520)).position.x) < 1e-9);
}

void dashboard_controls_are_visible_semantic_targets() {
  Settings settings;
  const auto buttons = dashboard_buttons(settings);
  CHECK(buttons.size() == 14);
  for (const auto& button : buttons) {
    CHECK(button.bounds.bottom - button.bounds.top >= 60.0F);
    CHECK(button.bounds.right - button.bounds.left >= 100.0F);
    const float x = (button.bounds.left + button.bounds.right) * 0.5F;
    const float y = (button.bounds.top + button.bounds.bottom) * 0.5F;
    const auto hit = dashboard_hit_test(buttons, x, y);
    if (button.enabled)
      CHECK(hit && *hit == button.action);
  }
  CHECK(!dashboard_hit_test(buttons, 80.0F, 130.0F));
  CHECK(!dashboard_hit_test(buttons, 610.0F, 88.0F));
  CHECK(!dashboard_hit_test(buttons, 228.0F, 173.0F));

  int selected = 0;
  for (const auto& button : buttons)
    if (button.selected)
      ++selected;
  CHECK(selected == 4);
}

void dashboard_tabs_have_distinct_large_targets() {
  const auto tabs = dashboard_tabs();
  CHECK(tabs[0].tab == DashboardTab::Settings);
  CHECK(tabs[1].tab == DashboardTab::Status);
  CHECK(tabs[2].tab == DashboardTab::Help);
  for (const auto& tab : tabs) {
    CHECK(tab.bounds.right - tab.bounds.left >= 60.0F);
    CHECK(tab.bounds.bottom - tab.bounds.top >= 60.0F);
    const float x = (tab.bounds.left + tab.bounds.right) * 0.5F;
    const float y = (tab.bounds.top + tab.bounds.bottom) * 0.5F;
    CHECK(dashboard_tab_hit_test(x, y) == tab.tab);
  }
  CHECK(!dashboard_tab_hit_test(354.0F, 126.0F));
  CHECK(!dashboard_tab_hit_test(512.0F, 180.0F));
  const auto value = dashboard_timeout_value_rect();
  CHECK(!dashboard_hit_test(dashboard_buttons(Settings{}), (value.left + value.right) * 0.5F,
                            (value.top + value.bottom) * 0.5F));
}

void dashboard_mouse_coordinates_match_buttons_at_any_mouse_scale() {
  const auto buttons = dashboard_buttons(Settings{});
  for (const auto& button : buttons) {
    if (!button.enabled)
      continue;
    const float x = (button.bounds.left + button.bounds.right) * 0.5F;
    const float y = (button.bounds.top + button.bounds.bottom) * 0.5F;
    for (const auto [mouse_width, mouse_height] :
         {std::pair{1024.0F, 456.0F}, std::pair{1920.0F, 855.0F}, std::pair{512.0F, 228.0F},
          std::pair{1024.0F, 1024.0F}}) {
      const auto point =
          dashboard_mouse_to_pixels(x * mouse_width / 1024.0F, (456.0F - y) * mouse_height / 456.0F,
                                    mouse_width, mouse_height, 1024.0F, 456.0F);
      CHECK(point);
      CHECK(dashboard_hit_test(buttons, point->x, point->y) == button.action);
    }
  }
  // A pointer below the first row must not hit it at either mouse scale. This
  // catches the previous top-left/bottom-left regression.
  for (const auto [mouse_width, mouse_height] :
       {std::pair{1024.0F, 456.0F}, std::pair{1920.0F, 855.0F}}) {
    const auto point = dashboard_mouse_to_pixels(300.0F * mouse_width / 1024.0F,
                                                 (456.0F - 448.0F) * mouse_height / 456.0F,
                                                 mouse_width, mouse_height, 1024.0F, 456.0F);
    CHECK(point);
    CHECK(!dashboard_hit_test(buttons, point->x, point->y));
  }
  CHECK(!dashboard_mouse_to_pixels(1, 1, 0, 456, 1024, 456));
  CHECK(!dashboard_mouse_to_pixels(-1, 1, 1024, 456, 1024, 456));
}

void dashboard_actions_change_only_the_intended_setting() {
  Settings settings;
  CHECK(settings.countdown_seconds == 3);
  CHECK(settings.control_timeout_seconds == 3);
  CHECK(apply_dashboard_action(settings, DashboardAction::SetRightHand));
  CHECK(settings.configured_hand == Hand::Right);
  CHECK(settings.countdown_seconds == 3);
  CHECK(apply_dashboard_action(settings, DashboardAction::SetCountdown15));
  CHECK(settings.countdown_seconds == 15);
  CHECK(apply_dashboard_action(settings, DashboardAction::IncreaseControlTimeout));
  CHECK(settings.control_timeout_seconds == 4);
  CHECK(apply_dashboard_action(settings, DashboardAction::DisableCountdownSound));
  CHECK(!settings.countdown_sound);
  CHECK(apply_dashboard_action(settings, DashboardAction::SetLanguageChinese));
  CHECK(settings.language == Language::ChineseSimplified);
  CHECK(!apply_dashboard_action(settings, DashboardAction::SetLanguageChinese));

  settings.control_timeout_seconds = 3;
  CHECK(!apply_dashboard_action(settings, DashboardAction::DecreaseControlTimeout));
  auto buttons = dashboard_buttons(settings);
  const auto disabled_minus = std::find_if(buttons.begin(), buttons.end(), [](const auto& button) {
    return button.action == DashboardAction::DecreaseControlTimeout;
  });
  CHECK(disabled_minus != buttons.end());
  CHECK(!disabled_minus->enabled);
  CHECK(!dashboard_hit_test(buttons,
                            (disabled_minus->bounds.left + disabled_minus->bounds.right) * 0.5F,
                            (disabled_minus->bounds.top + disabled_minus->bounds.bottom) * 0.5F));
}

void dashboard_timeout_buttons_repeat_while_held() {
  DashboardHoldRepeater repeater;
  const auto start = DashboardHoldRepeater::TimePoint{};

  repeater.press(DashboardAction::IncreaseControlTimeout, start);
  CHECK(!repeater.update(DashboardAction::IncreaseControlTimeout, start + 399ms));
  CHECK(repeater.update(DashboardAction::IncreaseControlTimeout, start + 400ms) ==
        DashboardAction::IncreaseControlTimeout);
  CHECK(!repeater.update(DashboardAction::IncreaseControlTimeout, start + 499ms));
  CHECK(repeater.update(DashboardAction::IncreaseControlTimeout, start + 500ms) ==
        DashboardAction::IncreaseControlTimeout);

  CHECK(!repeater.update(DashboardAction::DecreaseControlTimeout, start + 600ms));
  CHECK(!repeater.update(DashboardAction::IncreaseControlTimeout, start + 700ms));

  repeater.press(DashboardAction::DecreaseControlTimeout, start + 800ms);
  repeater.release();
  CHECK(!repeater.update(DashboardAction::DecreaseControlTimeout, start + 1200ms));

  repeater.press(DashboardAction::SetCountdown3, start + 1300ms);
  CHECK(!repeater.update(DashboardAction::SetCountdown3, start + 1800ms));
}

void state_machine_requires_all_prerequisites() {
  const auto identity = target();
  const std::vector<Prerequisites> unavailable{
      {false, true, true, true, true, identity}, {true, false, true, true, true, identity},
      {true, true, false, true, true, identity}, {true, true, true, false, true, identity},
      {true, true, true, true, false, identity}, {true, true, true, true, true, std::nullopt}};
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
  CHECK(has_effect(machine.on_confirm(at(20), target(), 3s), EffectType::ShowCountdownControl));
  CHECK(machine.snapshot(at(1020)).countdown_remaining == 2000ms);
  CHECK(std::abs(machine.snapshot(at(1020)).countdown_progress - 2.0 / 3.0) < 0.001);
  const auto countdown_complete = machine.tick(at(3020));
  CHECK(has_effect(countdown_complete, EffectType::RequestGracefulClose));
  CHECK(has_effect(countdown_complete, EffectType::HideControl));
  CHECK(machine.state() == FlowState::Exiting);
  CHECK(machine.on_cancel().empty());
  CHECK(has_effect(machine.on_graceful_result(at(3100), CloseRequestResult::TargetExited),
                   EffectType::HideHud));
  CHECK(machine.state() == FlowState::NoActiveFlow);
  CHECK(machine.snapshot(at(3100)).last_exit == LastExitStatus::Succeeded);
}

void state_machine_timeout_refresh_and_prerequisite_loss() {
  FlowStateMachine defaults;
  (void)defaults.on_gesture(at(0), ready());
  CHECK(has_effect(defaults.tick(at(2999)), EffectType::UpdateControlTimeout));
  CHECK(has_effect(defaults.tick(at(3000)), EffectType::HideControl));

  FlowStateMachine machine(2s);
  (void)machine.on_gesture(at(0), ready());
  CHECK(has_effect(machine.tick(at(1500)), EffectType::UpdateControlTimeout));
  // A repeated gesture refreshes only the idle deadline.
  (void)machine.on_gesture(at(1500), ready());
  CHECK(has_effect(machine.tick(at(3000)), EffectType::UpdateControlTimeout));
  CHECK(has_effect(machine.tick(at(3500)), EffectType::HideControl));

  (void)machine.on_gesture(at(4000), ready());
  (void)machine.on_control_clicked(at(4010));
  auto lost = ready();
  lost.opposite_pointer_available = false;
  CHECK(has_effect(machine.on_prerequisites(at(4020), lost), EffectType::HideControl));
  CHECK(machine.state() == FlowState::NoActiveFlow);
}

void state_machine_hover_pauses_and_restarts_control_timeout() {
  FlowStateMachine machine;
  (void)machine.on_gesture(at(0), ready());
  CHECK(std::abs(machine.snapshot(at(2000)).control_timeout_progress - 1.0 / 3.0) < 0.001);
  CHECK(has_effect(machine.on_control_hover(at(2000), true), EffectType::UpdateControlTimeout));
  CHECK(machine.snapshot(at(2000)).control_hovered);
  CHECK(machine.snapshot(at(2100)).control_timeout_progress > 0.6);
  CHECK(std::abs(machine.snapshot(at(2180)).control_timeout_progress - 1.0) < 0.001);
  CHECK(has_effect(machine.tick(at(10000)), EffectType::UpdateControlTimeout));
  CHECK(machine.state() == FlowState::IdleVisible);

  (void)machine.on_control_clicked(at(10000));
  CHECK(machine.state() == FlowState::Confirming);
  CHECK(has_effect(machine.tick(at(20000)), EffectType::UpdateControlTimeout));
  CHECK(machine.state() == FlowState::Confirming);
  CHECK(has_effect(machine.on_control_hover(at(20000), false), EffectType::UpdateControlTimeout));
  CHECK(!machine.snapshot(at(20000)).control_hovered);
  CHECK(std::abs(machine.snapshot(at(20000)).control_timeout_progress - 1.0) < 0.001);
  CHECK(has_effect(machine.tick(at(22999)), EffectType::UpdateControlTimeout));
  CHECK(has_effect(machine.tick(at(23000)), EffectType::HideControl));
  CHECK(machine.state() == FlowState::NoActiveFlow);
}

void state_machine_timeout_update_during_exit_applies_to_next_control() {
  FlowStateMachine machine(8s, 5s);
  (void)machine.on_gesture(at(0), ready());
  (void)machine.on_control_clicked(at(1));
  (void)machine.on_confirm(at(2), target(), 1s);
  (void)machine.tick(at(1002));
  CHECK(machine.state() == FlowState::Exiting);

  machine.set_control_timeout(3s);
  CHECK(machine.state() == FlowState::Exiting);
  CHECK(!has_effect(machine.tick(at(4000)), EffectType::RequestForceClose));
  (void)machine.on_target_status(at(4100), TargetStatus::Exited);
  CHECK(machine.state() == FlowState::NoActiveFlow);

  (void)machine.on_gesture(at(5000), ready());
  CHECK(machine.state() == FlowState::IdleVisible);
  CHECK(has_effect(machine.tick(at(7999)), EffectType::UpdateControlTimeout));
  CHECK(has_effect(machine.tick(at(8000)), EffectType::HideControl));
}

void state_machine_countdown_rules_and_target_lock() {
  FlowStateMachine machine;
  (void)machine.on_gesture(at(0), ready());
  (void)machine.on_control_clicked(at(1));
  CHECK(machine.on_confirm(at(2), target(43), 5s).empty());
  CHECK(machine.state() == FlowState::Confirming);
  CHECK(machine.on_confirm(at(2), target(), 2s).empty());
  const auto countdown_started = machine.on_confirm(at(2), target(), 5s);
  const auto show_countdown = std::find_if(
      countdown_started.begin(), countdown_started.end(),
      [](const Effect& effect) { return effect.type == EffectType::ShowCountdownControl; });
  CHECK(show_countdown != countdown_started.end());
  CHECK(show_countdown->target == target());

  const auto countdown_updated = machine.tick(at(50));
  const auto update_countdown = std::find_if(
      countdown_updated.begin(), countdown_updated.end(),
      [](const Effect& effect) { return effect.type == EffectType::UpdateCountdownControl; });
  CHECK(update_countdown != countdown_updated.end());
  CHECK(update_countdown->target == target());

  auto tracking_lost = ready();
  tracking_lost.headset_tracked = false;
  tracking_lost.configured_hand_tracked = false;
  tracking_lost.opposite_pointer_available = false;
  CHECK(machine.on_prerequisites(at(100), tracking_lost).empty());
  CHECK(machine.state() == FlowState::CountingDown);
  CHECK(machine.on_gesture(at(200), ready()).empty());
  CHECK(has_effect(machine.on_cancel(), EffectType::HideHud));
  CHECK(machine.snapshot(at(300)).last_exit == LastExitStatus::Cancelled);

  FlowStateMachine disconnected;
  (void)disconnected.on_gesture(at(0), ready());
  (void)disconnected.on_control_clicked(at(1));
  (void)disconnected.on_confirm(at(2), target(), 5s);
  auto hardware_lost = ready();
  hardware_lost.hardware_available = false;
  CHECK(has_effect(disconnected.on_prerequisites(at(100), hardware_lost), EffectType::HideHud));
  CHECK(disconnected.state() == FlowState::NoActiveFlow);
  CHECK(disconnected.snapshot(at(100)).last_exit == LastExitStatus::Cancelled);
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

void state_machine_new_flow_replaces_failure_hud_timer() {
  FlowStateMachine machine(8s, 5s);
  (void)machine.on_gesture(at(0), ready());
  (void)machine.on_control_clicked(at(1));
  (void)machine.on_confirm(at(2), target(), 1s);
  (void)machine.tick(at(1002));
  (void)machine.tick(at(6002));
  CHECK(has_effect(machine.on_force_result(at(7000), CloseRequestResult::Failed, "ACCESS_DENIED"),
                   EffectType::ShowFailureHud));

  const auto restarted = machine.on_gesture(at(8000), ready());
  CHECK(has_effect(restarted, EffectType::HideHud));
  CHECK(has_effect(restarted, EffectType::ShowIdleControl));
  (void)machine.on_control_clicked(at(8001));
  (void)machine.on_confirm(at(8002), target(), 1s);
  CHECK(machine.state() == FlowState::CountingDown);
  CHECK(!has_effect(machine.tick(at(11000)), EffectType::HideHud));
  CHECK(machine.state() == FlowState::Exiting);
}

void state_machine_graceful_request_failure_uses_force_fallback() {
  FlowStateMachine machine(8s, 5s);
  (void)machine.on_gesture(at(0), ready());
  (void)machine.on_control_clicked(at(1));
  (void)machine.on_confirm(at(2), target(), 1s);
  (void)machine.tick(at(1002));
  const auto failed = machine.on_graceful_result(at(1003), CloseRequestResult::Failed,
                                                 "EXIT_GRACEFUL_WINDOW_NOT_FOUND");
  CHECK(has_effect(failed, EffectType::Diagnostic));
  CHECK(!has_effect(failed, EffectType::ShowFailureHud));
  const auto diagnostic = std::find_if(failed.begin(), failed.end(), [](const Effect& effect) {
    return effect.type == EffectType::Diagnostic;
  });
  CHECK(diagnostic != failed.end());
  CHECK(diagnostic->code == "EXIT_GRACEFUL_WINDOW_NOT_FOUND");
  CHECK(diagnostic->target == target());
  CHECK(machine.state() == FlowState::Exiting);
  CHECK(machine.snapshot(at(1003)).last_exit == LastExitStatus::Never);
  CHECK(!has_effect(machine.tick(at(6001)), EffectType::RequestForceClose));
  CHECK(has_effect(machine.tick(at(6002)), EffectType::RequestForceClose));
}

void state_machine_force_request_remains_pending_until_exit() {
  FlowStateMachine machine(8s, 5s);
  (void)machine.on_gesture(at(0), ready());
  (void)machine.on_control_clicked(at(1));
  (void)machine.on_confirm(at(2), target(), 1s);
  (void)machine.tick(at(1002));
  CHECK(has_effect(machine.tick(at(6002)), EffectType::RequestForceClose));
  CHECK(has_effect(machine.on_force_result(at(6002), CloseRequestResult::Accepted),
                   EffectType::Diagnostic));
  CHECK(machine.state() == FlowState::Exiting);
  CHECK(machine.tick(at(10000)).empty());
  CHECK(machine.snapshot(at(10000)).last_exit == LastExitStatus::Never);
  CHECK(has_effect(machine.on_force_result(at(11000), CloseRequestResult::TargetExited),
                   EffectType::HideHud));
  CHECK(machine.snapshot(at(11000)).last_exit == LastExitStatus::Succeeded);
}

void state_machine_force_wait_timeout_releases_exiting() {
  FlowStateMachine machine(8s, 5s);
  (void)machine.on_gesture(at(0), ready());
  (void)machine.on_control_clicked(at(1));
  (void)machine.on_confirm(at(2), target(), 1s);
  (void)machine.tick(at(1002));
  CHECK(has_effect(machine.tick(at(6002)), EffectType::RequestForceClose));
  (void)machine.on_force_result(at(6002), CloseRequestResult::Accepted);
  CHECK(machine.tick(at(12001)).empty());
  CHECK(machine.state() == FlowState::Exiting);
  CHECK(has_effect(machine.tick(at(12002)), EffectType::ShowFailureHud));
  const auto snapshot = machine.snapshot(at(12002));
  CHECK(snapshot.state == FlowState::NoActiveFlow);
  CHECK(snapshot.last_exit == LastExitStatus::Failed);
  CHECK(snapshot.last_error_code == "EXIT_FORCE_WAIT_TIMEOUT");
  CHECK(machine.on_force_result(at(12003), CloseRequestResult::TargetExited).empty());
  CHECK(has_effect(machine.tick(at(16002)), EffectType::HideHud));
}

void state_machine_overlay_failure_aborts_before_exit() {
  FlowStateMachine machine;
  (void)machine.on_gesture(at(0), ready());
  CHECK(machine.state() == FlowState::IdleVisible);
  CHECK(has_effect(machine.on_overlay_failure("OVERLAY_PRESENT_FAILED"), EffectType::HideControl));
  CHECK(machine.state() == FlowState::NoActiveFlow);
  CHECK(machine.snapshot(at(1)).last_exit == LastExitStatus::Never);

  (void)machine.on_gesture(at(100), ready());
  (void)machine.on_control_clicked(at(101));
  (void)machine.on_confirm(at(102), target(), 1s);
  CHECK(machine.state() == FlowState::CountingDown);
  const auto effects = machine.on_overlay_failure("OVERLAY_PRESENT_FAILED");
  CHECK(has_effect(effects, EffectType::HideHud));
  CHECK(!has_effect(effects, EffectType::RequestGracefulClose));
  CHECK(machine.state() == FlowState::NoActiveFlow);
  CHECK(machine.snapshot(at(103)).last_exit == LastExitStatus::Failed);
  CHECK(machine.snapshot(at(103)).last_error_code == "OVERLAY_PRESENT_FAILED");
  CHECK(!has_effect(machine.tick(at(2000)), EffectType::RequestGracefulClose));
}

void state_machine_marks_every_exit_completion_for_resource_release() {
  const auto begin_exit = [] {
    FlowStateMachine machine(8s, 5s);
    (void)machine.on_gesture(at(0), ready());
    (void)machine.on_control_clicked(at(1));
    (void)machine.on_confirm(at(2), target(), 1s);
    (void)machine.tick(at(1002));
    CHECK(machine.state() == FlowState::Exiting);
    return machine;
  };

  auto graceful = begin_exit();
  CHECK(completes_exit(graceful.on_graceful_result(at(1003), CloseRequestResult::TargetExited)));

  auto identity_changed = begin_exit();
  CHECK(
      completes_exit(identity_changed.on_target_status(at(1003), TargetStatus::IdentityMismatch)));

  auto force_failed = begin_exit();
  (void)force_failed.tick(at(6002));
  CHECK(completes_exit(force_failed.on_force_result(at(6003), CloseRequestResult::Failed,
                                                    "EXIT_TERMINATE_PROCESS_FAILED")));

  auto timed_out = begin_exit();
  (void)timed_out.tick(at(6002));
  (void)timed_out.on_force_result(at(6003), CloseRequestResult::Accepted);
  CHECK(completes_exit(timed_out.tick(at(12002))));

  auto overlay_failed = begin_exit();
  CHECK(completes_exit(overlay_failed.on_overlay_failure("OVERLAY_PRESENT_FAILED")));

  const Effect unrelated{
      EffectType::Diagnostic, {}, {}, 0.0, "FLOW_COUNTDOWN_CANCELLED", FlowState::CountingDown,
      FlowState::NoActiveFlow};
  CHECK(!completes_exit_flow(unrelated));
}

}  // namespace

int main() {
  const std::vector<std::pair<std::string, std::function<void()>>> tests{
      {"gesture recognizes valid wave", gesture_recognizes_valid_wave},
      {"gesture recognizes compact chest wave", gesture_recognizes_compact_chest_wave},
      {"gesture recognizes center crossing wave", gesture_recognizes_center_crossing_wave},
      {"gesture recognizes sampled controller wave", gesture_recognizes_sampled_controller_wave},
      {"gesture uses head relative right axis", gesture_uses_head_relative_right_axis},
      {"gesture rejects unsafe or weak motion", gesture_rejects_unsafe_or_weak_motion},
      {"gesture accepts seated postures", gesture_accepts_real_seated_postures},
      {"gesture recognizes lazy fast low wave", gesture_recognizes_lazy_fast_low_wave},
      {"gesture rejects game input and body motion", gesture_rejects_game_input_and_body_motion},
      {"gesture rejects unstable and abrupt motion", gesture_rejects_unstable_and_abrupt_motion},
      {"gesture requires deliberate strokes", gesture_requires_deliberate_repeated_lateral_strokes},
      {"settings round trip and recovery", settings_round_trip_and_recovery},
      {"settings durable results", settings_worker_reports_durable_results_in_order},
      {"settings failure and queue bound",
       settings_worker_failure_preserves_last_commit_and_bounds_queue},
      {"settings concurrent stop", settings_worker_concurrent_stop_joins_once},
      {"localization resolves auto and manual", localization_resolves_auto_and_manual},
      {"diagnostics structured privacy", diagnostics_are_structured_and_privacy_bounded},
      {"diagnostics debug filtering", diagnostics_debug_records_follow_build_type},
      {"diagnostics async drain", diagnostics_async_queue_drains_in_order},
      {"diagnostics interrupted tail", diagnostics_recover_interrupted_jsonl_tail},
      {"diagnostics destructor joins", diagnostics_destructor_joins_writer},
      {"diagnostics bounded rotation", diagnostics_rotate_at_a_fixed_size},
      {"VR game classifier safety", vr_game_classifier_accepts_games_and_rejects_runtime_apps},
      {"pointer uses tracked controller", pointer_availability_uses_tracked_controller},
      {"wrist pose smoothing", wrist_pose_smoothing_follows_and_resets_safely},
      {"dashboard semantic targets", dashboard_controls_are_visible_semantic_targets},
      {"dashboard tabs", dashboard_tabs_have_distinct_large_targets},
      {"dashboard mouse scale", dashboard_mouse_coordinates_match_buttons_at_any_mouse_scale},
      {"dashboard actions", dashboard_actions_change_only_the_intended_setting},
      {"dashboard timeout hold repeat", dashboard_timeout_buttons_repeat_while_held},
      {"state requires prerequisites", state_machine_requires_all_prerequisites},
      {"state happy graceful path", state_machine_happy_graceful_path},
      {"state timeout and loss", state_machine_timeout_refresh_and_prerequisite_loss},
      {"state hover timeout", state_machine_hover_pauses_and_restarts_control_timeout},
      {"state timeout update during exit",
       state_machine_timeout_update_during_exit_applies_to_next_control},
      {"state countdown rules", state_machine_countdown_rules_and_target_lock},
      {"state target change", state_machine_target_change_is_safe_noop},
      {"state force failure", state_machine_force_fallback_and_failure_hud},
      {"state failure timer replacement", state_machine_new_flow_replaces_failure_hud_timer},
      {"state graceful request failure",
       state_machine_graceful_request_failure_uses_force_fallback},
      {"state force pending", state_machine_force_request_remains_pending_until_exit},
      {"state force wait timeout", state_machine_force_wait_timeout_releases_exiting},
      {"state overlay failure", state_machine_overlay_failure_aborts_before_exit},
      {"state exit completion releases resources",
       state_machine_marks_every_exit_completion_for_resource_release},
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
