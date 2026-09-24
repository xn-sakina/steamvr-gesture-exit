#pragma once
namespace bailout::platform::windows {
class CountdownSound {
 public:
  CountdownSound();
  ~CountdownSound();
  void tick(bool enabled);
  void ui_hover();
  void ui_select();

 private:
  unsigned char wave_[2048]{};
  unsigned char hover_wave_[44 + 320]{};
  unsigned char select_wave_[44 + 520]{};
};
}  // namespace bailout::platform::windows
