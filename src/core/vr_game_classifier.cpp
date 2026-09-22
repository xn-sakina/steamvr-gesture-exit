#include "core/vr_game_classifier.hpp"

#include <algorithm>
#include <array>
#include <string_view>

namespace bailout::core {
namespace {

std::string lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

}  // namespace

bool is_current_vr_game_candidate(const VrApplicationEvidence& evidence) {
  if (evidence.dashboard_overlay || evidence.template_application ||
      evidence.internal_application || evidence.hidden_application) {
    return false;
  }

  const std::string key = lower(evidence.app_key);
  // OpenVR exposes no game-vs-utility property. A scene app, a numeric Steam
  // key, and a binary/url launch can all belong to a non-game tool. Admit only
  // reviewed game keys; additional titles need an explicit safety review.
  constexpr std::array<std::string_view, 3> game_keys{
      "steam.app.438100",  // VRChat
      "steam.app.450390",  // The Lab
      "steam.app.620980",  // Beat Saber
  };
  if (std::find(game_keys.begin(), game_keys.end(), key) == game_keys.end())
    return false;

  const std::string launch_type = lower(evidence.launch_type);
  return launch_type == "binary" || launch_type == "url";
}

}  // namespace bailout::core
