#pragma once

#include "core/types.hpp"

#include <chrono>
#include <deque>
#include <string_view>

namespace bailout::core {

struct GestureConfig {
  std::chrono::milliseconds recognition_window{2400};
  std::chrono::milliseconds minimum_gesture_duration{600};
  std::chrono::milliseconds minimum_stroke_duration{140};
  std::chrono::milliseconds maximum_stroke_duration{750};
  // A farewell wave must contain four deliberate lateral strokes (three
  // reversals). Position is deliberately not height-gated: seated players may
  // start with a controller on an armrest or their lap.
  double minimum_stroke_metres{0.12};
  double minimum_total_lateral_travel_metres{0.40};
  double maximum_orthogonal_stroke_ratio{0.75};
  double maximum_stroke_length_ratio{2.5};
  double maximum_stroke_timing_ratio{2.75};
  int required_reversals{3};
  double minimum_head_distance_metres{0.18};
  double maximum_head_distance_metres{1.30};
  double maximum_behind_head_metres{0.20};
};

struct GestureSample {
  TimePoint time{};
  bool tracked{};
  Vec3 hand_position{};
  Vec3 headset_position{};
  Vec3 headset_forward{};
};

enum class GestureDecision {
  Reset,
  HandUntracked,
  TooClose,
  TooFar,
  BehindHead,
  WaitingForSamples,
  InsufficientStroke,
  InsufficientReversals,
  TooBrief,
  StrokeTooFast,
  StrokeTooSlow,
  NonLateralMotion,
  IrregularMotion,
  Recognized
};
[[nodiscard]] std::string_view to_string(GestureDecision) noexcept;

struct GestureDiagnostic {
  GestureDecision decision = GestureDecision::Reset;
  bool clock_reset = false;
  std::size_t sample_count = 0;
  int span_ms = 0;
  double head_distance_m = 0;
  double behind_head_m = 0;
  double amplitude_m = 0;
  double lateral_travel_m = 0;
  double maximum_orthogonal_ratio = 0;
  double stroke_length_ratio = 0;
  double stroke_timing_ratio = 0;
  int reversals = 0;
};

class GestureDetector {
 public:
  explicit GestureDetector(GestureConfig config = {});

  [[nodiscard]] bool update(const GestureSample& sample);
  void reset() noexcept;
  [[nodiscard]] const GestureDiagnostic& diagnostic() const noexcept { return diagnostic_; }
  [[nodiscard]] const GestureConfig& config() const noexcept { return config_; }

 private:
  [[nodiscard]] bool pose_is_eligible(const GestureSample& sample) noexcept;
  [[nodiscard]] bool motion_matches() noexcept;

  GestureConfig config_;
  std::deque<GestureSample> samples_;
  GestureDiagnostic diagnostic_{};
};

}  // namespace bailout::core
