#pragma once

#include <string>

namespace bailout::core {

struct VrApplicationEvidence {
  std::string app_key;
  std::string launch_type;
  bool dashboard_overlay = false;
  bool template_application = false;
  bool internal_application = false;
  bool hidden_application = false;
};

// Admits reviewed game keys using SteamVR metadata. The caller must also
// prove that this is the running scene app and maps to the same Windows process.
bool is_current_vr_game_candidate(const VrApplicationEvidence& evidence);

}  // namespace bailout::core
