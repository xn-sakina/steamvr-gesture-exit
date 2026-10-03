#include "core/gesture_detector.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace steamvr_gesture_exit::core {

GestureConfig gesture_config(const GestureSensitivity sensitivity) noexcept {
  GestureConfig config{};
  switch (sensitivity) {
    case GestureSensitivity::Low:
      config.minimum_stroke_metres = 0.105;
      config.minimum_total_lateral_travel_metres = 0.320;
      config.maximum_orthogonal_path_ratio = 0.60;
      config.minimum_stroke_efficiency = 0.72;
      config.maximum_stroke_length_ratio = 2.0;
      config.maximum_stroke_timing_ratio = 2.3;
      config.maximum_endpoint_drift_ratio = 0.50;
      config.maximum_endpoint_drift_metres = 0.050;
      config.minimum_confidence = 0.80;
      break;
    case GestureSensitivity::Normal:
      break;
    case GestureSensitivity::High:
      config.minimum_stroke_metres = 0.065;
      config.minimum_total_lateral_travel_metres = 0.200;
      config.maximum_orthogonal_path_ratio = 0.78;
      config.minimum_stroke_efficiency = 0.58;
      config.maximum_stroke_length_ratio = 2.8;
      config.maximum_stroke_timing_ratio = 3.2;
      config.maximum_endpoint_drift_ratio = 0.75;
      config.maximum_endpoint_drift_metres = 0.075;
      config.minimum_confidence = 0.66;
      break;
  }
  return config;
}

std::string_view to_string(GestureDecision decision) noexcept {
  switch (decision) {
    case GestureDecision::Reset:
      return "reset";
    case GestureDecision::HandUntracked:
      return "hand_untracked";
    case GestureDecision::TooClose:
      return "too_close";
    case GestureDecision::TooFar:
      return "too_far";
    case GestureDecision::BehindHead:
      return "behind_head";
    case GestureDecision::WaitingForSamples:
      return "waiting_for_samples";
    case GestureDecision::InsufficientStroke:
      return "insufficient_stroke";
    case GestureDecision::InsufficientReversals:
      return "insufficient_reversals";
    case GestureDecision::TooBrief:
      return "too_brief";
    case GestureDecision::StrokeTooFast:
      return "stroke_too_fast";
    case GestureDecision::StrokeTooSlow:
      return "stroke_too_slow";
    case GestureDecision::InputActive:
      return "input_active";
    case GestureDecision::AbruptMotion:
      return "abrupt_motion";
    case GestureDecision::NonLateralMotion:
      return "non_lateral_motion";
    case GestureDecision::UnstablePath:
      return "unstable_path";
    case GestureDecision::IrregularMotion:
      return "irregular_motion";
    case GestureDecision::Recognized:
      return "recognized";
  }
  return "reset";
}

GestureDetector::GestureDetector(GestureConfig config) : config_(config) {
  if (config_.recognition_window <= std::chrono::milliseconds::zero() ||
      config_.minimum_gesture_duration <= std::chrono::milliseconds::zero() ||
      config_.minimum_stroke_duration <= std::chrono::milliseconds::zero() ||
      config_.maximum_stroke_duration <= std::chrono::milliseconds::zero() ||
      config_.maximum_stroke_duration <= config_.minimum_stroke_duration ||
      config_.minimum_stroke_metres <= 0.0 || config_.minimum_total_lateral_travel_metres <= 0.0 ||
      config_.maximum_orthogonal_path_ratio <= 0.0 || config_.minimum_stroke_efficiency <= 0.0 ||
      config_.minimum_stroke_efficiency >= 1.0 || config_.maximum_stroke_length_ratio <= 1.0 ||
      config_.maximum_stroke_timing_ratio <= 1.0 || config_.maximum_endpoint_drift_ratio <= 0.0 ||
      config_.maximum_endpoint_drift_metres <= 0.0 ||
      config_.maximum_instantaneous_speed_metres_per_second <= 0.0 ||
      config_.minimum_confidence <= 0.0 || config_.minimum_confidence > 1.0 ||
      config_.required_reversals < 1 || config_.minimum_head_distance_metres <= 0.0 ||
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
                                             samples_.back().time - samples_.front().time)
                                             .count());
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
  struct Point {
    TimePoint time{};
    double lateral = 0;
    double vertical = 0;
    double depth = 0;
    bool input_active = false;
    Vec3 relative_position{};
    Vec3 head_right{};
  };
  std::vector<Point> points;
  points.reserve(samples_.size());
  for (const auto& sample : samples_) {
    Vec3 head_forward = normalized({sample.headset_forward.x, 0.0, sample.headset_forward.z});
    if (length(head_forward) < 0.5)
      head_forward = {0.0, 0.0, -1.0};
    Vec3 head_right = normalized({head_forward.z, 0.0, -head_forward.x});
    if (length(head_right) < 0.5)
      head_right = {1.0, 0.0, 0.0};
    const Vec3 relative = sample.hand_position - sample.headset_position;
    points.push_back({sample.time, dot(relative, head_right), relative.y,
                      dot(relative, head_forward), sample.input_active, relative, head_right});
  }

  // A short time-aware low-pass removes tracking shimmer without erasing a
  // quick human wave. Sparse synthetic samples retain almost all of their
  // displacement, while 90 Hz runtime noise is strongly attenuated.
  constexpr double kSmoothingSeconds = 0.035;
  for (std::size_t index = 1; index < points.size(); ++index) {
    const double seconds =
        std::chrono::duration<double>(points[index].time - points[index - 1].time).count();
    const double alpha = std::clamp(seconds / (kSmoothingSeconds + seconds), 0.0, 1.0);
    points[index].lateral =
        points[index - 1].lateral + alpha * (points[index].lateral - points[index - 1].lateral);
    points[index].vertical =
        points[index - 1].vertical + alpha * (points[index].vertical - points[index - 1].vertical);
    points[index].depth =
        points[index - 1].depth + alpha * (points[index].depth - points[index - 1].depth);
  }

  const auto [minimum, maximum] =
      std::minmax_element(points.begin(), points.end(),
                          [](const Point& a, const Point& b) { return a.lateral < b.lateral; });
  diagnostic_.amplitude_m = maximum->lateral - minimum->lateral;
  if (diagnostic_.amplitude_m < config_.minimum_stroke_metres) {
    diagnostic_.decision = GestureDecision::InsufficientStroke;
    return false;
  }

  // Extract extrema with hysteresis. A reversal is committed only after the
  // hand moves far enough away from the extreme; tremor near an endpoint
  // cannot manufacture direction changes. Full stroke distance is validated
  // separately so the endpoint itself remains accurate.
  std::vector<std::size_t> peaks;
  std::size_t start = 0;
  std::size_t extreme = 0;
  int direction = 0;
  const double start_hysteresis = config_.minimum_stroke_metres * 0.45;
  const double reversal_hysteresis = config_.minimum_stroke_metres * 0.38;
  for (std::size_t index = 1; index < points.size(); ++index) {
    const double from_start = points[index].lateral - points[start].lateral;
    if (direction == 0) {
      if (std::abs(from_start) < start_hysteresis)
        continue;
      direction = from_start > 0.0 ? 1 : -1;
      extreme = index;
      continue;
    }
    const double current = points[index].lateral;
    const double extreme_value = points[extreme].lateral;
    if ((direction > 0 && current >= extreme_value) ||
        (direction < 0 && current <= extreme_value)) {
      extreme = index;
      continue;
    }
    if (std::abs(current - extreme_value) < reversal_hysteresis)
      continue;
    peaks.push_back(extreme);
    direction = -direction;
    extreme = index;
  }
  if (direction == 0) {
    diagnostic_.decision = GestureDecision::InsufficientStroke;
    return false;
  }
  peaks.push_back(extreme);
  diagnostic_.reversals = std::max(0, static_cast<int>(peaks.size()) - 1);
  if (diagnostic_.reversals < config_.required_reversals) {
    diagnostic_.decision = GestureDecision::InsufficientReversals;
    return false;
  }

  // Evaluate only the most recent complete sequence of alternating extrema.
  // Earlier unrelated movement in the rolling history is excluded.
  const std::size_t peak_count = static_cast<std::size_t>(config_.required_reversals + 1);
  const auto first = peaks.end() - static_cast<std::ptrdiff_t>(peak_count);
  double lateral_travel = 0.0;
  double maximum_ratio = 0.0;
  double minimum_efficiency = 1.0;
  double maximum_speed = 0.0;
  double shortest_full_stroke = std::numeric_limits<double>::infinity();
  double longest_full_stroke = 0.0;
  auto shortest_full_duration = config_.maximum_stroke_duration;
  auto longest_full_duration = std::chrono::milliseconds::zero();
  bool input_active = false;
  for (std::size_t point_index = *first; point_index <= peaks.back(); ++point_index)
    input_active = input_active || points[point_index].input_active;
  diagnostic_.input_active = input_active;
  if (input_active) {
    diagnostic_.decision = GestureDecision::InputActive;
    return false;
  }

  for (auto it = first; it + 1 != peaks.end(); ++it) {
    const Point& a = points[*it];
    const Point& b = points[*(it + 1)];
    const double lateral = std::abs(b.lateral - a.lateral);

    // The head-relative axes intentionally rotate with the player, but that
    // also means head yaw can manufacture an apparent lateral stroke around a
    // nearly stationary controller. Keep the established trajectory model and
    // require each accepted stroke to be corroborated by a small, same-way
    // controller displacement measured through one shared axis. Using the
    // hand-to-head delta preserves whole-body translation cancellation, while
    // the shared axis prevents head rotation from becoming controller motion.
    // Reusing reversal hysteresis avoids changing the existing full-stroke
    // threshold for genuine waves.
    Vec3 stroke_right =
        normalized({a.head_right.x + b.head_right.x, 0.0, a.head_right.z + b.head_right.z});
    if (length(stroke_right) < 0.5)
      stroke_right = b.head_right;
    const double apparent_direction = b.lateral - a.lateral;
    const double controller_lateral = dot(b.relative_position - a.relative_position, stroke_right);
    if (controller_lateral * apparent_direction <= 0.0 ||
        std::abs(controller_lateral) < reversal_hysteresis) {
      diagnostic_.lateral_travel_m = lateral_travel;
      diagnostic_.decision = GestureDecision::InsufficientStroke;
      return false;
    }

    double lateral_path = 0.0;
    double orthogonal_path = 0.0;
    for (std::size_t point_index = *it + 1; point_index <= *(it + 1); ++point_index) {
      const Point& before = points[point_index - 1];
      const Point& current = points[point_index];
      const double lateral_step = std::abs(current.lateral - before.lateral);
      const double vertical_step = current.vertical - before.vertical;
      const double depth_step = current.depth - before.depth;
      const double orthogonal_step = std::hypot(vertical_step, depth_step);
      lateral_path += lateral_step;
      orthogonal_path += orthogonal_step;
      const double seconds = std::chrono::duration<double>(current.time - before.time).count();
      if (seconds > 0.0)
        maximum_speed =
            std::max(maximum_speed, std::hypot(lateral_step, orthogonal_step) / seconds);
    }
    const double ratio = lateral_path > 1e-9 ? orthogonal_path / lateral_path
                                             : std::numeric_limits<double>::infinity();
    const double efficiency = lateral_path > 1e-9 ? lateral / lateral_path : 0.0;
    lateral_travel += lateral;
    maximum_ratio = std::max(maximum_ratio, ratio);
    minimum_efficiency = std::min(minimum_efficiency, efficiency);
    const auto stroke_duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(b.time - a.time);
    if (lateral < config_.minimum_stroke_metres) {
      diagnostic_.lateral_travel_m = lateral_travel;
      diagnostic_.decision = GestureDecision::InsufficientStroke;
      return false;
    }
    if (stroke_duration < config_.minimum_stroke_duration) {
      diagnostic_.lateral_travel_m = lateral_travel;
      diagnostic_.maximum_orthogonal_ratio = maximum_ratio;
      diagnostic_.decision = GestureDecision::StrokeTooFast;
      return false;
    }
    if (stroke_duration > config_.maximum_stroke_duration) {
      diagnostic_.lateral_travel_m = lateral_travel;
      diagnostic_.maximum_orthogonal_ratio = maximum_ratio;
      diagnostic_.decision = GestureDecision::StrokeTooSlow;
      return false;
    }
    shortest_full_stroke = std::min(shortest_full_stroke, lateral);
    longest_full_stroke = std::max(longest_full_stroke, lateral);
    shortest_full_duration = std::min(shortest_full_duration, stroke_duration);
    longest_full_duration = std::max(longest_full_duration, stroke_duration);
  }
  diagnostic_.lateral_travel_m = lateral_travel;
  diagnostic_.maximum_orthogonal_ratio = maximum_ratio;
  diagnostic_.minimum_stroke_efficiency = minimum_efficiency;
  diagnostic_.maximum_speed_mps = maximum_speed;
  diagnostic_.stroke_length_ratio = longest_full_stroke / shortest_full_stroke;
  diagnostic_.stroke_timing_ratio =
      static_cast<double>(longest_full_duration.count()) / shortest_full_duration.count();
  const auto duration = points[peaks.back()].time - points[*first].time;
  diagnostic_.span_ms =
      static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
  if (duration < config_.minimum_gesture_duration) {
    diagnostic_.decision = GestureDecision::TooBrief;
    return false;
  }
  if (lateral_travel < config_.minimum_total_lateral_travel_metres) {
    diagnostic_.decision = GestureDecision::InsufficientStroke;
    return false;
  }
  if (maximum_speed > config_.maximum_instantaneous_speed_metres_per_second) {
    diagnostic_.decision = GestureDecision::AbruptMotion;
    return false;
  }
  if (maximum_ratio > config_.maximum_orthogonal_path_ratio) {
    diagnostic_.decision = GestureDecision::NonLateralMotion;
    return false;
  }
  if (minimum_efficiency < config_.minimum_stroke_efficiency) {
    diagnostic_.decision = GestureDecision::UnstablePath;
    return false;
  }

  double low_side_minimum = std::numeric_limits<double>::infinity();
  double low_side_maximum = -std::numeric_limits<double>::infinity();
  double high_side_minimum = std::numeric_limits<double>::infinity();
  double high_side_maximum = -std::numeric_limits<double>::infinity();
  const bool first_is_low = points[*first].lateral < points[*(first + 1)].lateral;
  std::size_t endpoint_index = 0;
  for (auto it = first; it != peaks.end(); ++it, ++endpoint_index) {
    const bool is_low = (endpoint_index % 2 == 0) == first_is_low;
    auto& minimum_value = is_low ? low_side_minimum : high_side_minimum;
    auto& maximum_value = is_low ? low_side_maximum : high_side_maximum;
    minimum_value = std::min(minimum_value, points[*it].lateral);
    maximum_value = std::max(maximum_value, points[*it].lateral);
  }
  diagnostic_.endpoint_drift_m =
      std::max(low_side_maximum - low_side_minimum, high_side_maximum - high_side_minimum);
  const double mean_stroke = lateral_travel / static_cast<double>(config_.required_reversals);
  const double endpoint_drift_limit = std::min(config_.maximum_endpoint_drift_metres,
                                               mean_stroke * config_.maximum_endpoint_drift_ratio);
  if (diagnostic_.endpoint_drift_m > endpoint_drift_limit) {
    diagnostic_.decision = GestureDecision::UnstablePath;
    return false;
  }

  // Aggregate independent evidence instead of making compact/faster motion
  // pass by distance alone. Clean repeated motion scores well even at lap
  // height; shaky, wandering or game-like motion loses confidence across
  // several dimensions.
  const auto quality = [](double value, double worst) {
    return std::clamp((worst - value) / (worst - 1.0), 0.0, 1.0);
  };
  const double dominance_quality =
      std::clamp(1.0 - maximum_ratio / config_.maximum_orthogonal_path_ratio, 0.0, 1.0);
  const double efficiency_quality =
      std::clamp((minimum_efficiency - config_.minimum_stroke_efficiency) /
                     (1.0 - config_.minimum_stroke_efficiency),
                 0.0, 1.0);
  const double endpoint_quality =
      endpoint_drift_limit > 1e-9
          ? std::clamp(1.0 - diagnostic_.endpoint_drift_m / endpoint_drift_limit, 0.0, 1.0)
          : 1.0;
  const double travel_quality =
      std::clamp((lateral_travel - config_.minimum_total_lateral_travel_metres) /
                     config_.minimum_total_lateral_travel_metres,
                 0.0, 1.0);
  diagnostic_.confidence =
      0.30 + 0.15 * dominance_quality + 0.15 * efficiency_quality +
      0.12 * quality(diagnostic_.stroke_length_ratio, config_.maximum_stroke_length_ratio) +
      0.12 * quality(diagnostic_.stroke_timing_ratio, config_.maximum_stroke_timing_ratio) +
      0.10 * endpoint_quality + 0.06 * travel_quality;
  if (diagnostic_.stroke_length_ratio > config_.maximum_stroke_length_ratio ||
      diagnostic_.stroke_timing_ratio > config_.maximum_stroke_timing_ratio ||
      diagnostic_.confidence < config_.minimum_confidence) {
    diagnostic_.decision = GestureDecision::IrregularMotion;
    return false;
  }
  diagnostic_.decision = GestureDecision::Recognized;
  return true;
}

void GestureDetector::reset() noexcept {
  samples_.clear();
  diagnostic_ = {};
}

}  // namespace steamvr_gesture_exit::core
