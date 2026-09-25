#include "core/gesture_detector.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace bailout::core {

std::string_view to_string(GestureDecision decision) noexcept {
  switch (decision) {
    case GestureDecision::Reset: return "reset";
    case GestureDecision::HandUntracked: return "hand_untracked";
    case GestureDecision::TooClose: return "too_close";
    case GestureDecision::TooFar: return "too_far";
    case GestureDecision::BehindHead: return "behind_head";
    case GestureDecision::WaitingForSamples: return "waiting_for_samples";
    case GestureDecision::InsufficientStroke: return "insufficient_stroke";
    case GestureDecision::InsufficientReversals: return "insufficient_reversals";
    case GestureDecision::TooBrief: return "too_brief";
    case GestureDecision::StrokeTooSlow: return "stroke_too_slow";
    case GestureDecision::NonLateralMotion: return "non_lateral_motion";
    case GestureDecision::Recognized: return "recognized";
  }
  return "reset";
}

GestureDetector::GestureDetector(GestureConfig config) : config_(config) {
  if (config_.recognition_window <= std::chrono::milliseconds::zero() ||
      config_.minimum_gesture_duration <= std::chrono::milliseconds::zero() ||
      config_.maximum_stroke_duration <= std::chrono::milliseconds::zero() ||
      config_.minimum_stroke_metres <= 0.0 ||
      config_.minimum_total_lateral_travel_metres <= 0.0 ||
      config_.maximum_orthogonal_stroke_ratio <= 0.0 || config_.required_reversals < 1 ||
      config_.minimum_head_distance_metres <= 0.0 ||
      config_.maximum_head_distance_metres <= config_.minimum_head_distance_metres ||
      config_.maximum_behind_head_metres < 0.0) {
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
  diagnostic_.behind_head_m = -dot(from_head, normalized(sample.headset_forward));
  if (distance < config_.minimum_head_distance_metres) {
    diagnostic_.decision = GestureDecision::TooClose;
    return false;
  }
  if (distance > config_.maximum_head_distance_metres) {
    diagnostic_.decision = GestureDecision::TooFar;
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
  if (samples_.size() < static_cast<std::size_t>(config_.required_reversals + 2)) {
    diagnostic_.decision = GestureDecision::WaitingForSamples;
    return false;
  }
  Vec3 head_forward = normalized({samples_.back().headset_forward.x, 0.0,
                                  samples_.back().headset_forward.z});
  if (length(head_forward) < 0.5)
    head_forward = {0.0, 0.0, -1.0};
  Vec3 head_right = normalized({head_forward.z, 0.0, -head_forward.x});
  if (length(head_right) < 0.5)
    head_right = {1.0, 0.0, 0.0};
  struct Point {
    TimePoint time{};
    double lateral = 0;
    double vertical = 0;
    double depth = 0;
  };
  std::vector<Point> points;
  points.reserve(samples_.size());
  for (const auto& sample : samples_) {
    const Vec3 relative = sample.hand_position - sample.headset_position;
    points.push_back(
        {sample.time, dot(relative, head_right), relative.y, dot(relative, head_forward)});
  }

  const auto [minimum, maximum] = std::minmax_element(
      points.begin(), points.end(),
      [](const Point& a, const Point& b) { return a.lateral < b.lateral; });
  diagnostic_.amplitude_m = maximum->lateral - minimum->lateral;
  if (diagnostic_.amplitude_m < config_.minimum_stroke_metres) {
    diagnostic_.decision = GestureDecision::InsufficientStroke;
    return false;
  }

  // Extract only committed turning points. A reversal is not counted until
  // the controller has completed a full stroke in the opposite direction;
  // tiny oscillations around an extreme therefore cannot inflate the count.
  std::vector<std::size_t> turns;
  std::size_t start = 0;
  std::size_t extreme = 0;
  int direction = 0;
  for (std::size_t index = 1; index < points.size(); ++index) {
    const double from_start = points[index].lateral - points[start].lateral;
    if (direction == 0) {
      // A user commonly begins near the centre of the wave, so the first
      // half-stroke only arms the recognizer. Every reversal after it still
      // requires the full configured stroke distance.
      if (std::abs(from_start) < config_.minimum_stroke_metres * 0.5)
        continue;
      direction = from_start > 0.0 ? 1 : -1;
      extreme = index;
      turns = {start};
      continue;
    }
    const double current = points[index].lateral;
    const double extreme_value = points[extreme].lateral;
    if ((direction > 0 && current >= extreme_value) ||
        (direction < 0 && current <= extreme_value)) {
      extreme = index;
      continue;
    }
    if (std::abs(current - extreme_value) < config_.minimum_stroke_metres)
      continue;
    turns.push_back(extreme);
    direction = -direction;
    extreme = index;
  }
  if (direction == 0) {
    diagnostic_.decision = GestureDecision::InsufficientStroke;
    return false;
  }
  turns.push_back(extreme);
  diagnostic_.reversals = std::max(0, static_cast<int>(turns.size()) - 2);
  if (diagnostic_.reversals < config_.required_reversals) {
    diagnostic_.decision = GestureDecision::InsufficientReversals;
    return false;
  }

  // Evaluate the most recent complete farewell sequence so unrelated movement
  // at the beginning of the rolling window cannot make a valid wave fail.
  const std::size_t point_count = static_cast<std::size_t>(config_.required_reversals + 2);
  const auto first = turns.end() - static_cast<std::ptrdiff_t>(point_count);
  double lateral_travel = 0.0;
  double maximum_ratio = 0.0;
  std::size_t stroke_index = 0;
  for (auto it = first; it + 1 != turns.end(); ++it) {
    const Point& a = points[*it];
    const Point& b = points[*(it + 1)];
    const double lateral = std::abs(b.lateral - a.lateral);
    const double orthogonal = std::hypot(b.vertical - a.vertical, b.depth - a.depth);
    const double ratio = lateral > 1e-9 ? orthogonal / lateral
                                       : std::numeric_limits<double>::infinity();
    lateral_travel += lateral;
    maximum_ratio = std::max(maximum_ratio, ratio);
    // The first entry may include an arbitrary stationary prelude before the
    // user begins moving. Timing starts once that arming half-stroke reaches
    // its first extreme; all subsequent full strokes are bounded.
    if (stroke_index > 0 && b.time - a.time > config_.maximum_stroke_duration) {
      diagnostic_.lateral_travel_m = lateral_travel;
      diagnostic_.maximum_orthogonal_ratio = maximum_ratio;
      diagnostic_.decision = GestureDecision::StrokeTooSlow;
      return false;
    }
    ++stroke_index;
  }
  diagnostic_.lateral_travel_m = lateral_travel;
  diagnostic_.maximum_orthogonal_ratio = maximum_ratio;
  const auto duration = points[turns.back()].time - points[*(first + 1)].time;
  diagnostic_.span_ms = static_cast<int>(
      std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
  if (duration < config_.minimum_gesture_duration) {
    diagnostic_.decision = GestureDecision::TooBrief;
    return false;
  }
  if (lateral_travel < config_.minimum_total_lateral_travel_metres) {
    diagnostic_.decision = GestureDecision::InsufficientStroke;
    return false;
  }
  if (maximum_ratio > config_.maximum_orthogonal_stroke_ratio) {
    diagnostic_.decision = GestureDecision::NonLateralMotion;
    return false;
  }
  diagnostic_.decision = GestureDecision::Recognized;
  return true;
}

void GestureDetector::reset() noexcept {
  samples_.clear();
  diagnostic_ = {};
}

}  // namespace bailout::core
