#pragma once

#include "core/types.hpp"

#include <chrono>
#include <deque>
#include <string_view>

namespace bailout::core {

struct GestureConfig {
  std::chrono::milliseconds recognition_window{2000};
  double minimum_amplitude_metres{0.10};
  double reversal_deadband_metres{0.025};
  double minimum_head_distance_metres{0.20};
  double maximum_head_distance_metres{1.25};
  double maximum_behind_head_metres{0.10};
  // Head-relative upper-body proxy; the MVP has no anatomical torso tracking.
  double maximum_hand_below_head_metres{0.70};
};

struct GestureSample {
  TimePoint time{};
  bool tracked{};
  Vec3 hand_position{};
  Vec3 headset_position{};
  Vec3 headset_forward{};
};

enum class GestureDecision {
  Reset, HandUntracked, TooClose, TooFar, TooLow, BehindHead,
  WaitingForSamples, InsufficientAmplitude, InsufficientReversals, Recognized
};
[[nodiscard]] std::string_view to_string(GestureDecision) noexcept;

struct GestureDiagnostic {
  GestureDecision decision = GestureDecision::Reset;
  bool clock_reset = false;
  std::size_t sample_count = 0;
  int span_ms = 0;
  double head_distance_m = 0;
  double below_head_m = 0;
  double behind_head_m = 0;
  double amplitude_m = 0;
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
