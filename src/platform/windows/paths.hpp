#pragma once
#include <filesystem>
namespace bailout::platform::windows {
std::filesystem::path data_directory();
std::filesystem::path settings_path();
std::filesystem::path log_directory();
std::filesystem::path executable_path();
}
