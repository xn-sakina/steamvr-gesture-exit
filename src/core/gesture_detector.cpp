#include "core/gesture_detector.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace bailout::core {

std::string_view to_string(GestureDecision decision) noexcept {
  switch (decision) {
    case GestureDecision::Reset: return "reset";
    case GestureDecision::HandUntracked: return "hand_untracked";
    case GestureDecision::TooClose: return "too_close";
    case GestureDecision::TooFar: return "too_far";
    case GestureDecision::TooLow: return "too_low";
    case GestureDecision::BehindHead: return "behind_head";
    case GestureDecision::WaitingForSamples: return "waiting_for_samples";
    case GestureDecision::InsufficientAmplitude: return "insufficient_amplitude";
    case GestureDecision::InsufficientReversals: return "insufficient_reversals";
    case GestureDecision::Recognized: return "recognized";
  }
  return "reset";
}

GestureDetector::GestureDetector(GestureConfig config) : config_(config) {
  if (config_.recognition_window <= std::chrono::milliseconds::zero() ||
      config_.minimum_amplitude_metres <= 0.0 || config_.reversal_deadband_metres <= 0.0 ||
      config_.maximum_hand_below_head_metres <= 0.0) {
    throw std::invalid_argument("invalid farewell gesture configuration");
  }
}

bool GestureDetector::pose_is_eligible(const GestureSample& sample) noexcept {
  if (!sample.tracked) {
    diagnostic_.decision = GestureDecision::HandUntracked;
    return false;
  }
  const Vec3 from_head = sample.hand_position - sample.headset_position;
  const double distance = length(from_head);
  diagnostic_.head_distance_m = distance;
  diagnostic_.below_head_m = -from_head.y;
  diagnostic_.behind_head_m = -dot(from_head, normalized(sample.headset_forward));
  if (distance < config_.minimum_head_distance_metres) {
    diagnostic_.decision = GestureDecision::TooClose;
    return false;
  }
  if (distance > config_.maximum_head_distance_metres) {
    diagnostic_.decision = GestureDecision::TooFar;
    return false;
  }
  if (from_head.y < -config_.maximum_hand_below_head_metres) {
    diagnostic_.decision = GestureDecision::TooLow;
    return false;
  }
  if (diagnostic_.behind_head_m > config_.maximum_behind_head_metres) {
    diagnostic_.decision = GestureDecision::BehindHead;
    return false;
  }
  // A generic controller pose has no portable anatomical palm direction.
  return true;
}

bool GestureDetector::update(const GestureSample& sample) {
  const bool clock_reset = !samples_.empty() && sample.time <= samples_.back().time;
  diagnostic_ = {};
  diagnostic_.clock_reset = clock_reset;
  if (clock_reset)
    samples_.clear();
  if (!pose_is_eligible(sample)) {
    samples_.clear();
    return false;
  }
  samples_.push_back(sample);
  const auto cutoff = sample.time - config_.recognition_window;
  while (!samples_.empty() && samples_.front().time < cutoff) {
    samples_.pop_front();
  }
  diagnostic_.sample_count = samples_.size();
  diagnostic_.span_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
      samples_.back().time - samples_.front().time).count());
  if (!motion_matches()) {
    return false;
  }
  diagnostic_.decision = GestureDecision::Recognized;
  samples_.clear();
  return true;
}

bool GestureDetector::motion_matches() noexcept {
  if (samples_.size() < 4) {
    diagnostic_.decision = GestureDecision::WaitingForSamples;
    return false;
  }
  const Vec3 forward = normalized(samples_.back().headset_forward);
  Vec3 head_right = normalized({forward.z, 0.0, -forward.x});
  if (length(head_right) < 0.5)
    head_right = {1.0, 0.0, 0.0};
  const auto coordinate = [head_right](const GestureSample& sample) {
    return dot(sample.hand_position - sample.headset_position, head_right);
  };
  const auto [minimum, maximum] = std::minmax_element(
      samples_.begin(), samples_.end(),
      [&](const auto& a, const auto& b) { return coordinate(a) < coordinate(b); });
  diagnostic_.amplitude_m = coordinate(*maximum) - coordinate(*minimum);
  if (diagnostic_.amplitude_m < config_.minimum_amplitude_metres) {
    diagnostic_.decision = GestureDecision::InsufficientAmplitude;
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
  diagnostic_.reversals = reversals;
  diagnostic_.decision = reversals >= 2 ? GestureDecision::Recognized
                                        : GestureDecision::InsufficientReversals;
  return reversals >= 2;
}

void GestureDetector::reset() noexcept {
  samples_.clear();
  diagnostic_ = {};
}

}  // namespace bailout::core
