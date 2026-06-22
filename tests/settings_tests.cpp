#include "windows_platform.h"
#include "test_utils.h"

namespace {

void valid_settings_round_trip() {
  auto settings = sbo::default_settings();
  settings.configuredHand = sbo::Hand::left;
  settings.exitCountdownDurationSeconds = 10;
  settings.language = sbo::LanguageMode::chinese;

  auto result = sbo::parse_settings_json(sbo::settings_to_json(settings));
  CHECK_TRUE(!result.recovered);
  CHECK_EQ(result.settings.configuredHand, sbo::Hand::left);
  CHECK_EQ(result.settings.exitCountdownDurationSeconds, 10);
  CHECK_EQ(result.settings.language, sbo::LanguageMode::chinese);
}

void corrupted_settings_recover_defaults() {
  auto result = sbo::parse_settings_json("{\"schemaVersion\":1,\"configuredHand\":\"middle\"}");
  CHECK_TRUE(result.recovered);
  CHECK_EQ(result.settings.configuredHand, sbo::Hand::right);
  CHECK_EQ(result.settings.exitCountdownDurationSeconds, 5);
  CHECK_TRUE(result.settings.countdownSoundEnabled);
}

void language_auto_uses_steam_then_fallback() {
  CHECK_EQ(sbo::LanguageService::resolve(sbo::LanguageMode::autoDetect, "schinese"), std::string("zh"));
  CHECK_EQ(sbo::LanguageService::resolve(sbo::LanguageMode::english, "schinese"), std::string("en"));
}

}  // namespace

void run_settings_tests() {
  valid_settings_round_trip();
  corrupted_settings_recover_defaults();
  language_auto_uses_steam_then_fallback();
}
