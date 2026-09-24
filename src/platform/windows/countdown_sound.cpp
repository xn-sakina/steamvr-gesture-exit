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
void make_wave(unsigned char* wave, uint32_t samples, double frequency, double amplitude) {
  constexpr uint32_t rate = 8000;
  std::memcpy(wave, "RIFF", 4);
  put<uint32_t>(wave + 4, 36 + samples);
  std::memcpy(wave + 8, "WAVEfmt ", 8);
  put<uint32_t>(wave + 16, 16);
  put<uint16_t>(wave + 20, 1);
  put<uint16_t>(wave + 22, 1);
  put<uint32_t>(wave + 24, rate);
  put<uint32_t>(wave + 28, rate);
  put<uint16_t>(wave + 32, 1);
  put<uint16_t>(wave + 34, 8);
  std::memcpy(wave + 36, "data", 4);
  put<uint32_t>(wave + 40, samples);
  for (uint32_t i = 0; i < samples; ++i) {
    const double attack = std::min(1.0, static_cast<double>(i) / 32.0);
    const double fade = 1.0 - static_cast<double>(i) / samples;
    const double sample = std::sin(2.0 * 3.141592653589793 * frequency * i / rate);
    wave[44 + i] = static_cast<unsigned char>(
        128 + std::clamp(sample * amplitude * attack * fade, -amplitude, amplitude));
  }
}
}  // namespace
CountdownSound::CountdownSound() {
  make_wave(wave_, 800, 660.0, 20.0);
  make_wave(hover_wave_, 320, 540.0, 13.0);
  make_wave(select_wave_, 520, 820.0, 19.0);
}
CountdownSound::~CountdownSound() {
  // SND_MEMORY | SND_ASYNC keeps using wave_ after tick() returns.
  PlaySoundW(nullptr, nullptr, 0);
}
void CountdownSound::tick(bool enabled) {
  if (enabled)
    PlaySoundW(reinterpret_cast<LPCWSTR>(wave_), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
void CountdownSound::ui_hover() {
  PlaySoundW(reinterpret_cast<LPCWSTR>(hover_wave_), nullptr,
             SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
void CountdownSound::ui_select() {
  PlaySoundW(reinterpret_cast<LPCWSTR>(select_wave_), nullptr,
             SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
}  // namespace bailout::platform::windows
