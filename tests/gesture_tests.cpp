#include "gesture.h"
#include "test_utils.h"

using namespace std::chrono_literals;

namespace {

sbo::PoseSample sample(std::chrono::steady_clock::time_point t, double x) {
  return {t, {0.0, 1.6, 0.0}, {x, 1.45, 0.35}, {x, -0.1, 0.35}, {0.0, 1.0, 0.0}, true};
}

void valid_wave_is_recognized() {
  sbo::GestureRecognizer recognizer;
  const auto now = std::chrono::steady_clock::now();
  CHECK_TRUE(!recognizer.add_sample(sample(now, -0.12)));
  CHECK_TRUE(!recognizer.add_sample(sample(now + 100ms, 0.12)));
  CHECK_TRUE(!recognizer.add_sample(sample(now + 200ms, -0.11)));
  CHECK_TRUE(!recognizer.add_sample(sample(now + 300ms, 0.13)));
  CHECK_TRUE(recognizer.add_sample(sample(now + 400ms, -0.12)));
}

void jitter_is_rejected() {
  sbo::GestureRecognizer recognizer;
  const auto now = std::chrono::steady_clock::now();
  bool recognized = false;
  for (int i = 0; i < 8; ++i) {
    recognized = recognized || recognizer.add_sample(sample(now + i * 100ms, (i % 2 == 0) ? 0.01 : -0.01));
  }
  CHECK_TRUE(!recognized);
}

void downward_hand_is_rejected() {
  sbo::GestureRecognizer recognizer;
  const auto now = std::chrono::steady_clock::now();
  bool recognized = false;
  for (int i = 0; i < 5; ++i) {
    auto s = sample(now + i * 100ms, (i % 2 == 0) ? -0.2 : 0.2);
    s.handUp = {0.0, -1.0, 0.0};
    recognized = recognized || recognizer.add_sample(s);
  }
  CHECK_TRUE(!recognized);
}

}  // namespace

void run_gesture_tests() {
  valid_wave_is_recognized();
  jitter_is_rejected();
  downward_hand_is_rejected();
}
