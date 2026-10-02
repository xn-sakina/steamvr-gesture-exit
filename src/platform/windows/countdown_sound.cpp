#include "platform/windows/countdown_sound.hpp"
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
namespace steamvr_gesture_exit::platform::windows {
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
void make_bubble_wave(unsigned char* wave) {
  constexpr uint32_t samples = 960;
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

  double phase = 0.0;
  for (uint32_t index = 0; index < samples; ++index) {
    const double position = static_cast<double>(index) / samples;
    // A short upward chirp with a rounded decay reads as a soft message
    // bubble instead of the previous notification beep.
    const double frequency = 430.0 + 520.0 * std::sqrt(position);
    phase += 2.0 * 3.141592653589793 * frequency / rate;
    const double attack = std::min(1.0, static_cast<double>(index) / 18.0);
    const double decay = std::exp(-4.8 * position);
    const double body = std::sin(phase) * 18.0 * attack * decay;
    const double pop = std::sin(phase * 0.48) * 5.0 * attack * std::exp(-18.0 * position);
    wave[44 + index] = static_cast<unsigned char>(
        128 + std::clamp(body + pop, -24.0, 24.0));
  }
}
}  // namespace
CountdownSound::CountdownSound() {
  make_wave(wave_, 800, 660.0, 20.0);
  make_bubble_wave(open_wave_);
  make_wave(hover_wave_, 320, 540.0, 13.0);
  make_wave(select_wave_, 520, 820.0, 19.0);
}
CountdownSound::~CountdownSound() {
  // SND_MEMORY | SND_ASYNC keeps using a member wave buffer after playback returns.
  PlaySoundW(nullptr, nullptr, 0);
}
void CountdownSound::tick(bool enabled) {
  if (enabled)
    PlaySoundW(reinterpret_cast<LPCWSTR>(wave_), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
void CountdownSound::ui_open() {
  PlaySoundW(reinterpret_cast<LPCWSTR>(open_wave_), nullptr,
             SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
void CountdownSound::ui_hover() {
  PlaySoundW(reinterpret_cast<LPCWSTR>(hover_wave_), nullptr,
             SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
void CountdownSound::ui_select() {
  PlaySoundW(reinterpret_cast<LPCWSTR>(select_wave_), nullptr,
             SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
}  // namespace steamvr_gesture_exit::platform::windows
