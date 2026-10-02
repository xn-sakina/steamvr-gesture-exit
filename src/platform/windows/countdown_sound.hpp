#pragma once
namespace steamvr_gesture_exit::platform::windows {
class CountdownSound {
 public:
  CountdownSound();
  ~CountdownSound();
  void tick(bool enabled);
  void ui_open();
  void ui_hover();
  void ui_select();

 private:
  unsigned char wave_[2048]{};
  unsigned char open_wave_[44 + 720]{};
  unsigned char hover_wave_[44 + 320]{};
  unsigned char select_wave_[44 + 520]{};
};
}  // namespace steamvr_gesture_exit::platform::windows
