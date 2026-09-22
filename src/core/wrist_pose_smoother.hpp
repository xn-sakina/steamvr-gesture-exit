#pragma once

#include "core/types.hpp"
#include <optional>

namespace bailout::core {

struct WristPose {
  Vec3 position;
  Vec3 right;
  Vec3 up;
  Vec3 back;
};

// Smooths a world-space overlay pose while preserving an orthonormal basis.
// The first sample, a tracking gap, and a large tracking jump snap to the
// current controller pose so an old control never sweeps across the scene.
class WristPoseSmoother {
 public:
  [[nodiscard]] WristPose update(WristPose target, TimePoint time) noexcept;
  void reset() noexcept;

 private:
  std::optional<WristPose> pose_;
  TimePoint previous_time_{};
};

}  // namespace bailout::core
