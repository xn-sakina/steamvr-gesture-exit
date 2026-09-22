#include "core/vr_game_classifier.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace bailout::core {
namespace {

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

bool starts_with(const std::string& value, std::string_view prefix) {
  return value.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), value.begin());
}

bool is_numeric_steam_key(const std::string& key) {
  constexpr std::string_view prefix = "steam.app.";
  if (!starts_with(key, prefix) || key.size() == prefix.size())
    return false;
  return std::all_of(key.begin() + static_cast<std::ptrdiff_t>(prefix.size()), key.end(),
                     [](unsigned char character) { return std::isdigit(character) != 0; });
}

bool is_registered_custom_key(const std::string& key) {
  if (key.empty() || key.front() == '.' || key.back() == '.' ||
      key.find('.') == std::string::npos || key.find("..") != std::string::npos) {
    return false;
  }
  return std::all_of(key.begin(), key.end(), [](unsigned char character) {
    return std::isalnum(character) != 0 || character == '.' || character == '-' || character == '_';
  });
}

}  // namespace

bool is_current_vr_game_candidate(const VrApplicationEvidence& evidence) {
  if (evidence.dashboard_overlay || evidence.template_application ||
      evidence.internal_application || evidence.hidden_application) {
    return false;
  }

  const std::string key = lower(evidence.app_key);
  if (key.empty() || key == "com.steamvr-bail-out.overlay")
    return false;

  // These namespaces are owned by the runtime, overlays, tools, or generated
  // unknown applications. Unknown targets deliberately remain a Safe No-op.
  constexpr std::array<std::string_view, 9> reserved_prefixes{
      "openvr.",           "steam.overlay.", "steamvr.",    "com.valvesoftware.steamvr",
      "system.generated.", "vrmonitor",      "vrdashboard", "vrserver",
      "vrcompositor"};
  for (const auto prefix : reserved_prefixes) {
    if (starts_with(key, prefix))
      return false;
  }

  const bool steam_key = starts_with(key, "steam.app.");
  if ((steam_key && !is_numeric_steam_key(key)) || (!steam_key && !is_registered_custom_key(key))) {
    return false;
  }

  const std::string launch_type = lower(evidence.launch_type);
  return launch_type == "binary" || launch_type == "url";
}

}  // namespace bailout::core
