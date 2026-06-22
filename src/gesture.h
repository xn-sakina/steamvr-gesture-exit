#pragma once

#include "flow.h"

#include <chrono>
#include <deque>

namespace sbo {

struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct PoseSample {
  std::chrono::steady_clock::time_point time;
  Vec3 headsetPosition;
  Vec3 handPosition;
  Vec3 palmDirection;
  Vec3 handUp;
  bool valid = false;
  Vec3 headsetRight{1.0, 0.0, 0.0};
  Vec3 headsetForward{0.0, 0.0, -1.0};
};

class GestureRecognizer {
 public:
  explicit GestureRecognizer(std::chrono::milliseconds window = std::chrono::milliseconds(1200));
  bool add_sample(const PoseSample& sample);
  void reset();

 private:
  bool in_activation_zone(const PoseSample& sample) const;
  bool palm_outward(const PoseSample& sample) const;
  bool non_downward(const PoseSample& sample) const;
  bool has_wave_motion() const;

  std::chrono::milliseconds window_;
  std::deque<PoseSample> samples_;
};

double dot(Vec3 a, Vec3 b);
double length(Vec3 v);
Vec3 subtract(Vec3 a, Vec3 b);

}  // namespace sbo
