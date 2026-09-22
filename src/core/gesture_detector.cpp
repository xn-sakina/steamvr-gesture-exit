#include "core/gesture_detector.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bailout::core {

GestureDetector::GestureDetector(GestureConfig config) : config_(config) {
  if (config_.recognition_window <= std::chrono::milliseconds::zero() ||
      config_.minimum_amplitude_metres <= 0.0 ||
      config_.reversal_deadband_metres <= 0.0 ||
      config_.maximum_hand_below_head_metres <= 0.0) {
    throw std::invalid_argument("invalid farewell gesture configuration");
  }
}

bool GestureDetector::pose_is_eligible(const GestureSample& sample) const noexcept {
  if (!sample.tracked) {
    return false;
  }
  const Vec3 from_head = sample.hand_position - sample.headset_position;
  const double distance = length(from_head);
  if (distance < config_.minimum_head_distance_metres ||
      distance > config_.maximum_head_distance_metres) {
    return false;
  }
  if (from_head.y < -config_.maximum_hand_below_head_metres) {
    return false;
  }
  const Vec3 head_forward = normalized(sample.headset_forward);
  if (dot(from_head, head_forward) < -config_.maximum_behind_head_metres) {
    return false;
  }
  const Vec3 palm = normalized(sample.palm_normal);
  if (length(palm) < 0.5 || palm.y < config_.minimum_palm_up_component) {
    return false;
  }
  return dot(palm, normalized(from_head)) >= config_.minimum_outward_palm_dot;
}

bool GestureDetector::update(const GestureSample& sample) {
  if (!samples_.empty() && sample.time <= samples_.back().time) {
    reset();
  }
  if (!pose_is_eligible(sample)) {
    reset();
    return false;
  }
  samples_.push_back(sample);
  const auto cutoff = sample.time - config_.recognition_window;
  while (!samples_.empty() && samples_.front().time < cutoff) {
    samples_.pop_front();
  }
  if (!motion_matches()) {
    return false;
  }
  reset();
  return true;
}

bool GestureDetector::motion_matches() const noexcept {
  if (samples_.size() < 4) {
    return false;
  }
  const Vec3 forward = normalized(samples_.back().headset_forward);
  Vec3 head_right = normalized({forward.z, 0.0, -forward.x});
  if (length(head_right) < 0.5) head_right = {1.0, 0.0, 0.0};
  const auto coordinate = [head_right](const GestureSample& sample) {
    return dot(sample.hand_position - sample.headset_position, head_right);
  };
  const auto [minimum, maximum] = std::minmax_element(
      samples_.begin(), samples_.end(),
      [&](const auto& a, const auto& b) { return coordinate(a) < coordinate(b); });
  if (coordinate(*maximum) - coordinate(*minimum) < config_.minimum_amplitude_metres) {
    return false;
  }

  int reversals = 0;
  int previous_direction = 0;
  double anchor = coordinate(samples_.front());
  for (std::size_t index = 1; index < samples_.size(); ++index) {
    const double current = coordinate(samples_[index]);
    const double delta = current - anchor;
    if (std::abs(delta) < config_.reversal_deadband_metres) {
      continue;
    }
    const int direction = delta > 0.0 ? 1 : -1;
    if (previous_direction != 0 && direction != previous_direction) {
      ++reversals;
    }
    previous_direction = direction;
    anchor = current;
  }
  return reversals >= 2;
}

void GestureDetector::reset() noexcept { samples_.clear(); }

}  // namespace bailout::core
