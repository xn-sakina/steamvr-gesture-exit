#include "gesture.h"

#include <cmath>

namespace sbo {

namespace {
constexpr double kMinAmplitudeMeters = 0.18;
constexpr double kDirectionNoiseMeters = 0.035;
constexpr double kPalmOutwardDot = 0.20;
constexpr double kMinHeightAboveHeadset = -0.35;
constexpr double kMaxForwardDistance = 0.85;
constexpr double kMaxBehindDistance = -0.35;
}

double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

double length(Vec3 v) { return std::sqrt(dot(v, v)); }

Vec3 subtract(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

GestureRecognizer::GestureRecognizer(std::chrono::milliseconds window) : window_(window) {}

void GestureRecognizer::reset() { samples_.clear(); }

bool GestureRecognizer::add_sample(const PoseSample& sample) {
  if (!sample.valid) {
    reset();
    return false;
  }

  samples_.push_back(sample);
  while (!samples_.empty() && sample.time - samples_.front().time > window_) {
    samples_.pop_front();
  }

  if (samples_.size() < 5) return false;
  if (!in_activation_zone(sample) || !palm_outward(sample) || !non_downward(sample)) return false;
  if (!has_wave_motion()) return false;

  reset();
  return true;
}

bool GestureRecognizer::in_activation_zone(const PoseSample& sample) const {
  const Vec3 relative = subtract(sample.handPosition, sample.headsetPosition);
  const double forwardLen = length(sample.headsetForward);
  const double forward = forwardLen < 0.001 ? -relative.z : dot(relative, sample.headsetForward) / forwardLen;
  return relative.y >= kMinHeightAboveHeadset && forward >= kMaxBehindDistance && forward <= kMaxForwardDistance;
}

bool GestureRecognizer::palm_outward(const PoseSample& sample) const {
  const Vec3 fromHead = subtract(sample.handPosition, sample.headsetPosition);
  const double fromHeadLen = length(fromHead);
  const double palmLen = length(sample.palmDirection);
  if (fromHeadLen < 0.001 || palmLen < 0.001) return false;
  return dot(sample.palmDirection, fromHead) / (palmLen * fromHeadLen) >= kPalmOutwardDot;
}

bool GestureRecognizer::non_downward(const PoseSample& sample) const {
  return sample.handUp.y > -0.25;
}

bool GestureRecognizer::has_wave_motion() const {
  if (samples_.size() < 5) return false;
  auto lateral_position = [](const PoseSample& sample) {
    const Vec3 relative = subtract(sample.handPosition, sample.headsetPosition);
    const double rightLen = length(sample.headsetRight);
    if (rightLen < 0.001) return relative.x;
    return dot(relative, sample.headsetRight) / rightLen;
  };

  double previousX = lateral_position(samples_.front());
  double minX = previousX;
  double maxX = previousX;
  int reversals = 0;
  int previousDirection = 0;

  for (std::size_t i = 1; i < samples_.size(); ++i) {
    const double x = lateral_position(samples_[i]);
    minX = std::min(minX, x);
    maxX = std::max(maxX, x);
    const double dx = x - previousX;
    previousX = x;
    int direction = 0;
    if (dx > kDirectionNoiseMeters) direction = 1;
    if (dx < -kDirectionNoiseMeters) direction = -1;
    if (direction != 0) {
      if (previousDirection != 0 && direction != previousDirection) ++reversals;
      previousDirection = direction;
    }
  }

  return (maxX - minX) >= kMinAmplitudeMeters && reversals >= 2;
}

}  // namespace sbo
