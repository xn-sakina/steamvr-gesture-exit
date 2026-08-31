#pragma once

#include "core/types.hpp"

#include <chrono>
#include <deque>

namespace bailout::core {

struct GestureConfig {
  std::chrono::milliseconds recognition_window{1200};
  double minimum_amplitude_metres{0.14};
  double reversal_deadband_metres{0.025};
  double minimum_head_distance_metres{0.20};
  double maximum_head_distance_metres{1.25};
  double maximum_behind_head_metres{0.10};
  double minimum_outward_palm_dot{0.35};
  double minimum_palm_up_component{-0.45};
};

struct GestureSample {
  TimePoint time{};
  bool tracked{};
  Vec3 hand_position{};
  Vec3 palm_normal{};
  Vec3 headset_position{};
  Vec3 headset_forward{};
};

class GestureDetector {
 public:
  explicit GestureDetector(GestureConfig config = {});

  [[nodiscard]] bool update(const GestureSample& sample);
  void reset() noexcept;

 private:
  [[nodiscard]] bool pose_is_eligible(const GestureSample& sample) const noexcept;
  [[nodiscard]] bool motion_matches() const noexcept;

  GestureConfig config_;
  std::deque<GestureSample> samples_;
};

}  // namespace bailout::core
