#pragma once

#include "core/types.hpp"

#include <chrono>
#include <deque>
#include <string_view>

namespace bailout::core {

struct GestureConfig {
  std::chrono::milliseconds recognition_window{1800};
  std::chrono::milliseconds minimum_gesture_duration{300};
  std::chrono::milliseconds minimum_stroke_duration{75};
  std::chrono::milliseconds maximum_stroke_duration{650};
  // Three committed reversals provide the repeated intent evidence. The
  // smaller stroke floor accepts a lazy armrest/lap wave, while path quality,
  // endpoint stability, input suppression and the confidence threshold keep
  // this from becoming a simple relaxed-distance detector.
  double minimum_stroke_metres{0.075};
  double minimum_total_lateral_travel_metres{0.235};
  double maximum_orthogonal_path_ratio{0.70};
  double minimum_stroke_efficiency{0.65};
  double maximum_stroke_length_ratio{2.4};
  double maximum_stroke_timing_ratio{2.8};
  double maximum_endpoint_drift_ratio{0.65};
  double maximum_endpoint_drift_metres{0.065};
  double maximum_instantaneous_speed_metres_per_second{2.8};
  double minimum_confidence{0.72};
  int required_reversals{3};
  double minimum_head_distance_metres{0.18};
  double maximum_head_distance_metres{1.30};
  double maximum_behind_head_metres{0.20};
};

// Sensitivity adjusts how much clean farewell-motion evidence is required.
// Safety invariants such as input suppression, repeated reversals, plausible
// speed, tracking continuity and pose independence remain unchanged.
[[nodiscard]] GestureConfig gesture_config(GestureSensitivity sensitivity) noexcept;

struct GestureSample {
  TimePoint time{};
  bool tracked{};
  Vec3 hand_position{};
  Vec3 headset_position{};
  Vec3 headset_forward{};
  // True while the configured controller is actively manipulating the game
  // (button pressed or a legacy OpenVR axis displaced). An intentional
  // Farewell Gesture is button-free.
  bool input_active{};
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
  InputActive,
  AbruptMotion,
  NonLateralMotion,
  UnstablePath,
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
  double minimum_stroke_efficiency = 0;
  double stroke_length_ratio = 0;
  double stroke_timing_ratio = 0;
  double endpoint_drift_m = 0;
  double maximum_speed_mps = 0;
  double confidence = 0;
  bool input_active = false;
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
