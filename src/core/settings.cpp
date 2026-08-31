#include "core/settings.hpp"

#include <array>
#include <fstream>
#include <regex>
#include <sstream>
#include <system_error>

namespace bailout::core {
namespace {

std::optional<std::string> string_value(const std::string& json, const char* key) {
  const std::regex pattern(std::string{"\""} + key + "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch match;
  if (!std::regex_search(json, match, pattern)) {
    return std::nullopt;
  }
  return match[1].str();
}

std::optional<int> integer_value(const std::string& json, const char* key) {
  const std::regex pattern(std::string{"\""} + key + "\"\\s*:\\s*(-?[0-9]+)");
  std::smatch match;
  if (!std::regex_search(json, match, pattern)) {
    return std::nullopt;
  }
  try {
    return std::stoi(match[1].str());
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<bool> boolean_value(const std::string& json, const char* key) {
  const std::regex pattern(std::string{"\""} + key + "\"\\s*:\\s*(true|false)");
  std::smatch match;
  if (!std::regex_search(json, match, pattern)) {
    return std::nullopt;
  }
  return match[1].str() == "true";
}

std::optional<Hand> parse_hand(const std::string& value) {
  if (value == "left") return Hand::Left;
  if (value == "right") return Hand::Right;
  return std::nullopt;
}

std::optional<Language> parse_language(const std::string& value) {
  if (value == "auto") return Language::Auto;
  if (value == "en") return Language::English;
  if (value == "zh-CN") return Language::ChineseSimplified;
  return std::nullopt;
}

}  // namespace

Settings Settings::defaults() noexcept { return {}; }

std::optional<std::string> Settings::validation_error() const {
  if (version != current_version) return "SETTINGS_VERSION_UNSUPPORTED";
  constexpr std::array allowed_countdowns{1, 3, 5, 10, 15};
  bool allowed = false;
  for (const int value : allowed_countdowns) allowed |= countdown_seconds == value;
  if (!allowed) return "SETTINGS_COUNTDOWN_INVALID";
  if (control_timeout_seconds < 3 || control_timeout_seconds > 30) {
    return "SETTINGS_CONTROL_TIMEOUT_INVALID";
  }
  return std::nullopt;
}

std::string to_string(const Hand value) {
  return value == Hand::Left ? "left" : "right";
}

std::string to_string(const Language value) {
  switch (value) {
    case Language::Auto: return "auto";
    case Language::English: return "en";
    case Language::ChineseSimplified: return "zh-CN";
  }
  return "auto";
}

SettingsStore::SettingsStore(std::filesystem::path path) : path_(std::move(path)) {}

SettingsLoadResult SettingsStore::load() const {
  std::error_code ec;
  if (!std::filesystem::exists(path_, ec)) {
    return {Settings::defaults(), SettingsLoadStatus::DefaultsMissing, "SETTINGS_MISSING"};
  }
  std::ifstream input(path_, std::ios::binary);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (!input.good() && !input.eof()) {
    return {Settings::defaults(), SettingsLoadStatus::RecoveredCorrupt, "SETTINGS_READ_FAILED"};
  }
  const std::string json = buffer.str();
  const auto version = integer_value(json, "version");
  if (version && *version != Settings::current_version) {
    return {Settings::defaults(), SettingsLoadStatus::RecoveredIncompatible,
            "SETTINGS_VERSION_UNSUPPORTED"};
  }
  const auto hand_text = string_value(json, "configuredHand");
  const auto countdown = integer_value(json, "countdownSeconds");
  const auto timeout = integer_value(json, "controlTimeoutSeconds");
  const auto sound = boolean_value(json, "countdownSound");
  const auto language_text = string_value(json, "language");
  if (!version || !hand_text || !countdown || !timeout || !sound || !language_text) {
    return {Settings::defaults(), SettingsLoadStatus::RecoveredCorrupt,
            "SETTINGS_JSON_INVALID"};
  }
  const auto hand = parse_hand(*hand_text);
  const auto language = parse_language(*language_text);
  if (!hand || !language) {
    return {Settings::defaults(), SettingsLoadStatus::RecoveredCorrupt,
            "SETTINGS_VALUE_INVALID"};
  }
  Settings settings{*version, *hand, *countdown, *timeout, *sound, *language};
  if (const auto error = settings.validation_error()) {
    return {Settings::defaults(), SettingsLoadStatus::RecoveredCorrupt, *error};
  }
  return {settings, SettingsLoadStatus::Loaded, {}};
}

bool SettingsStore::save(const Settings& settings, std::string* error) const {
  if (const auto validation = settings.validation_error()) {
    if (error) *error = *validation;
    return false;
  }
  std::error_code ec;
  if (!path_.parent_path().empty()) {
    std::filesystem::create_directories(path_.parent_path(), ec);
    if (ec) {
      if (error) *error = "SETTINGS_CREATE_DIRECTORY_FAILED";
      return false;
    }
  }
  auto temporary = path_;
  temporary += ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << "{\n"
           << "  \"version\": " << settings.version << ",\n"
           << "  \"configuredHand\": \"" << to_string(settings.configured_hand) << "\",\n"
           << "  \"countdownSeconds\": " << settings.countdown_seconds << ",\n"
           << "  \"controlTimeoutSeconds\": " << settings.control_timeout_seconds << ",\n"
           << "  \"countdownSound\": " << (settings.countdown_sound ? "true" : "false") << ",\n"
           << "  \"language\": \"" << to_string(settings.language) << "\"\n"
           << "}\n";
    output.flush();
    if (!output) {
      if (error) *error = "SETTINGS_WRITE_FAILED";
      return false;
    }
  }
  std::filesystem::rename(temporary, path_, ec);
  if (ec) {
    // Windows rename does not replace existing files, so remove just the known
    // settings destination and retry. The temporary file remains recoverable if
    // the second operation fails.
    std::filesystem::remove(path_, ec);
    ec.clear();
    std::filesystem::rename(temporary, path_, ec);
  }
  if (ec) {
    if (error) *error = "SETTINGS_REPLACE_FAILED";
    return false;
  }
  return true;
}

}  // namespace bailout::core
