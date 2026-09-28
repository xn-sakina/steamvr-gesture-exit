#include "core/settings.hpp"
#include "json/json.h"

#include <array>
#include <exception>
#include <fstream>
#include <sstream>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
#endif

namespace bailout::core {
namespace {

bool json_document_boundaries_valid(const std::string& document, const Json::Value& root) {
  const auto end = root.getOffsetLimit();
  if (end > document.size())
    return false;
  const auto whitespace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
  for (size_t i = end; i < document.size(); ++i) {
    if (!whitespace(document[i]))
      return false;
  }

  // The bundled JsonCpp version accepts a comma immediately before a closing
  // bracket. Reject that syntax while retaining its JSON string decoding.
  bool in_string = false;
  bool escaped = false;
  for (size_t i = 0; i < end; ++i) {
    const char c = document[i];
    if (in_string) {
      if (static_cast<unsigned char>(c) < 0x20)
        return false;
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        in_string = false;
    } else if (c == '"') {
      in_string = true;
    } else {
      if (static_cast<unsigned char>(c) < 0x20 && !whitespace(c))
        return false;
      if (c == ',') {
        size_t next = i + 1;
        while (next < end && whitespace(document[next]))
          ++next;
        if (next < end && (document[next] == '}' || document[next] == ']')) {
          return false;
        }
      }
    }
  }
  return !in_string;
}

std::optional<Hand> parse_hand(const std::string& value) {
  if (value == "left")
    return Hand::Left;
  if (value == "right")
    return Hand::Right;
  return std::nullopt;
}

std::optional<Language> parse_language(const std::string& value) {
  if (value == "auto")
    return Language::Auto;
  if (value == "en")
    return Language::English;
  if (value == "zh-CN")
    return Language::ChineseSimplified;
  return std::nullopt;
}

std::optional<GestureSensitivity> parse_gesture_sensitivity(const std::string& value) {
  if (value == "low")
    return GestureSensitivity::Low;
  if (value == "normal")
    return GestureSensitivity::Normal;
  if (value == "high")
    return GestureSensitivity::High;
  return std::nullopt;
}

void discard_temporary_file(const std::filesystem::path& path) {
  auto temporary = path;
  temporary += ".tmp";
  std::error_code ec;
  if (std::filesystem::is_regular_file(temporary, ec))
    std::filesystem::remove(temporary, ec);
}

}  // namespace

Settings Settings::defaults() noexcept {
  return {};
}

std::optional<std::string> Settings::validation_error() const {
  if (version != current_version)
    return "SETTINGS_VERSION_UNSUPPORTED";
  constexpr std::array allowed_countdowns{1, 3, 5, 10, 15};
  bool allowed = false;
  for (const int value : allowed_countdowns)
    allowed |= countdown_seconds == value;
  if (!allowed)
    return "SETTINGS_COUNTDOWN_INVALID";
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
    case Language::Auto:
      return "auto";
    case Language::English:
      return "en";
    case Language::ChineseSimplified:
      return "zh-CN";
  }
  return "auto";
}

std::string to_string(const GestureSensitivity value) {
  switch (value) {
    case GestureSensitivity::Low:
      return "low";
    case GestureSensitivity::Normal:
      return "normal";
    case GestureSensitivity::High:
      return "high";
  }
  return "normal";
}

SettingsStore::SettingsStore(std::filesystem::path path) : path_(std::move(path)) {}

SettingsLoadResult SettingsStore::load() const {
  // A terminated writer can leave only the staging file behind. The committed
  // settings file remains authoritative after an interrupted atomic replace.
  discard_temporary_file(path_);
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
  Json::CharReaderBuilder parser;
  parser["allowComments"] = false;
  parser["strictRoot"] = true;
  parser["failIfExtra"] = true;
  parser["rejectDupKeys"] = true;
  Json::Value root;
  std::string parse_error;
  const std::string document = buffer.str();
  std::istringstream json(document);
  bool parsed = false;
  try {
    parsed = Json::parseFromStream(parser, json, &root, &parse_error);
  } catch (const std::exception&) {
    parsed = false;
  }
  if (!parsed || !root.isObject() || !json_document_boundaries_valid(document, root)) {
    return {Settings::defaults(), SettingsLoadStatus::RecoveredCorrupt, "SETTINGS_JSON_INVALID"};
  }
  const auto& version = root["version"];
  if (!version.isInt()) {
    return {Settings::defaults(), SettingsLoadStatus::RecoveredCorrupt, "SETTINGS_JSON_INVALID"};
  }
  const int document_version = version.asInt();
  if (document_version != 1 && document_version != Settings::current_version) {
    return {Settings::defaults(), SettingsLoadStatus::RecoveredIncompatible,
            "SETTINGS_VERSION_UNSUPPORTED"};
  }
  const auto& hand_text = root["configuredHand"];
  const auto& countdown = root["countdownSeconds"];
  const auto& timeout = root["controlTimeoutSeconds"];
  const auto& sound = root["countdownSound"];
  const auto& language_text = root["language"];
  const bool legacy = document_version == 1;
  const auto expected_size = legacy ? 6U : 7U;
  if (root.size() != expected_size || !hand_text.isString() || !countdown.isInt() ||
      !timeout.isInt() || !sound.isBool() || !language_text.isString()) {
    return {Settings::defaults(), SettingsLoadStatus::RecoveredCorrupt, "SETTINGS_JSON_INVALID"};
  }
  const auto hand = parse_hand(hand_text.asString());
  const auto language = parse_language(language_text.asString());
  std::optional<GestureSensitivity> sensitivity{GestureSensitivity::Normal};
  if (!legacy) {
    const auto& sensitivity_text = root["gestureSensitivity"];
    sensitivity = sensitivity_text.isString()
                      ? parse_gesture_sensitivity(sensitivity_text.asString())
                      : std::nullopt;
  }
  if (!hand || !language || !sensitivity) {
    return {Settings::defaults(), SettingsLoadStatus::RecoveredCorrupt, "SETTINGS_VALUE_INVALID"};
  }
  Settings settings{Settings::current_version,
                    *hand,
                    countdown.asInt(),
                    timeout.asInt(),
                    sound.asBool(),
                    *language,
                    *sensitivity};
  if (const auto error = settings.validation_error()) {
    return {Settings::defaults(), SettingsLoadStatus::RecoveredCorrupt, *error};
  }
  return {settings, SettingsLoadStatus::Loaded, {}};
}

bool SettingsStore::save(const Settings& settings, std::string* error) const {
  if (const auto validation = settings.validation_error()) {
    if (error)
      *error = *validation;
    return false;
  }
  std::error_code ec;
  if (!path_.parent_path().empty()) {
    std::filesystem::create_directories(path_.parent_path(), ec);
    if (ec) {
      if (error)
        *error = "SETTINGS_CREATE_DIRECTORY_FAILED";
      return false;
    }
  }
  auto temporary = path_;
  temporary += ".tmp";
  bool written = false;
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << "{\n"
           << "  \"version\": " << settings.version << ",\n"
           << "  \"configuredHand\": \"" << to_string(settings.configured_hand) << "\",\n"
           << "  \"countdownSeconds\": " << settings.countdown_seconds << ",\n"
           << "  \"controlTimeoutSeconds\": " << settings.control_timeout_seconds << ",\n"
           << "  \"countdownSound\": " << (settings.countdown_sound ? "true" : "false") << ",\n"
           << "  \"language\": \"" << to_string(settings.language) << "\",\n"
           << "  \"gestureSensitivity\": \"" << to_string(settings.gesture_sensitivity) << "\"\n"
           << "}\n";
    output.flush();
    written = static_cast<bool>(output);
    output.close();
    written = written && static_cast<bool>(output);
  }
  if (!written) {
    discard_temporary_file(path_);
    if (error)
      *error = "SETTINGS_WRITE_FAILED";
    return false;
  }
  // Flush the staging file before publishing it as the committed settings.
#ifdef _WIN32
  HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  const bool flushed = file != INVALID_HANDLE_VALUE && FlushFileBuffers(file) != 0;
  if (file != INVALID_HANDLE_VALUE)
    CloseHandle(file);
  if (!flushed) {
    discard_temporary_file(path_);
    if (error)
      *error = "SETTINGS_FLUSH_FAILED";
    return false;
  }
#endif
  // Keep the existing settings file intact if replacing it fails.
#ifdef _WIN32
  if (!MoveFileExW(temporary.c_str(), path_.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    ec = std::error_code(static_cast<int>(GetLastError()), std::system_category());
  }
#else
  std::filesystem::rename(temporary, path_, ec);
#endif
  if (ec) {
    discard_temporary_file(path_);
    if (error)
      *error = "SETTINGS_REPLACE_FAILED";
    return false;
  }
  return true;
}

}  // namespace bailout::core
