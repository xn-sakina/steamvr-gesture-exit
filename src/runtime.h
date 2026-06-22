#pragma once

#include "diagnostics.h"
#include "flow.h"
#include "gesture.h"
#include "windows_platform.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sbo {

class RuntimeSession {
 public:
  RuntimeSession();
  ~RuntimeSession();
  int run();
  void request_stop();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

std::vector<std::uint8_t> render_overlay_rgba(const RenderModel& model, int width, int height, bool dashboard);

}  // namespace sbo
