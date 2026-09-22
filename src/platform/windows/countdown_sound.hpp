#pragma once
namespace bailout::platform::windows {
class CountdownSound {
 public:
  CountdownSound();
  void tick(bool enabled);

 private:
  unsigned char wave_[2048]{};
};
}  // namespace bailout::platform::windows
