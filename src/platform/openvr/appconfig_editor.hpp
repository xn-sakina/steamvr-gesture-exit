#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace steamvr_gesture_exit::platform::openvr {

struct AppConfigEditResult {
  std::string document;
  std::string error;
  std::size_t removed = 0;
  bool added = false;
};

bool remove_owned_manifest_paths(std::string_view document, std::string_view app_key,
                                 const std::vector<std::filesystem::path>& known_paths,
                                 AppConfigEditResult& result);
bool add_manifest_path(std::string_view document, std::string_view manifest_path,
                       AppConfigEditResult& result);

}  // namespace steamvr_gesture_exit::platform::openvr
