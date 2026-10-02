#pragma once
#include <filesystem>
namespace steamvr_gesture_exit::platform::windows {
std::filesystem::path data_directory();
std::filesystem::path settings_path();
std::filesystem::path last_exit_path();
std::filesystem::path log_directory();
std::filesystem::path executable_path();
}  // namespace steamvr_gesture_exit::platform::windows
