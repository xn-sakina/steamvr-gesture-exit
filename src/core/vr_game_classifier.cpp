#include "core/vr_game_classifier.hpp"
#include "core/supported_vr_games.generated.hpp"

#include <algorithm>
#include <cctype>

namespace steamvr_gesture_exit::core {
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
  // OpenVR exposes no game-vs-utility property. The reviewed keys are generated
  // from config/supported_vr_games.json so contributors can update evidence and
  // scope without editing classifier logic.
  if (std::find(generated::kSupportedVrGameKeys.begin(), generated::kSupportedVrGameKeys.end(),
                key) == generated::kSupportedVrGameKeys.end())
    return false;

  const std::string launch_type = lower(evidence.launch_type);
  // The launch type is optional metadata for a reviewed Steam game key. An
  // omitted value does not weaken the scene PID, key and process checks.
  return launch_type.empty() || launch_type == "binary" || launch_type == "url";
}

}  // namespace steamvr_gesture_exit::core
