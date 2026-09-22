#include "platform/windows/countdown_sound.hpp"
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace bailout::platform::windows {
namespace {
template <class T>
void put(unsigned char* p, T v) {
  std::memcpy(p, &v, sizeof(v));
}
}  // namespace
CountdownSound::CountdownSound() {
  constexpr uint32_t rate = 8000, samples = 800;
  constexpr uint16_t channels = 1, bits = 8;
  constexpr uint32_t bytes = samples;
  std::memcpy(wave_, "RIFF", 4);
  put<uint32_t>(wave_ + 4, 36 + bytes);
  std::memcpy(wave_ + 8, "WAVEfmt ", 8);
  put<uint32_t>(wave_ + 16, 16);
  put<uint16_t>(wave_ + 20, 1);
  put<uint16_t>(wave_ + 22, channels);
  put<uint32_t>(wave_ + 24, rate);
  put<uint32_t>(wave_ + 28, rate);
  put<uint16_t>(wave_ + 32, 1);
  put<uint16_t>(wave_ + 34, bits);
  std::memcpy(wave_ + 36, "data", 4);
  put<uint32_t>(wave_ + 40, bytes);
  for (uint32_t i = 0; i < samples; ++i) {
    double fade = 1.0 - static_cast<double>(i) / samples;
    double s = std::sin(2.0 * 3.141592653589793 * 660.0 * i / rate);
    wave_[44 + i] = static_cast<unsigned char>(128 + std::clamp(s * 20.0 * fade, -20.0, 20.0));
  }
}
void CountdownSound::tick(bool enabled) {
  if (enabled)
    PlaySoundW(reinterpret_cast<LPCWSTR>(wave_), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
}  // namespace bailout::platform::windows
