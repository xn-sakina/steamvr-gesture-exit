#include "windows_platform.h"

#include <fstream>
#include <regex>
#include <sstream>

#if defined(_WIN32)
#define NOMINMAX
#include <Windows.h>
#include <ShlObj.h>
#include <Psapi.h>
#endif

namespace sbo {

namespace {

std::optional<std::string> match_string(const std::string& text, const char* key) {
  const std::regex re(std::string("\"") + key + "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch m;
  if (std::regex_search(text, m, re)) return m[1].str();
  return std::nullopt;
}

std::optional<int> match_int(const std::string& text, const char* key) {
  const std::regex re(std::string("\"") + key + "\"\\s*:\\s*(-?[0-9]+)");
  std::smatch m;
  if (std::regex_search(text, m, re)) return std::stoi(m[1].str());
  return std::nullopt;
}

std::optional<bool> match_bool(const std::string& text, const char* key) {
  const std::regex re(std::string("\"") + key + "\"\\s*:\\s*(true|false)");
  std::smatch m;
  if (std::regex_search(text, m, re)) return m[1].str() == "true";
  return std::nullopt;
}

std::string hand_to_json(Hand hand) { return hand == Hand::left ? "left" : "right"; }

std::string language_to_json(LanguageMode mode) {
  switch (mode) {
    case LanguageMode::autoDetect: return "auto";
    case LanguageMode::english: return "en";
    case LanguageMode::chinese: return "zh";
  }
  return "auto";
}

std::optional<Hand> parse_hand(const std::string& value) {
  if (value == "left") return Hand::left;
  if (value == "right") return Hand::right;
  return std::nullopt;
}

std::optional<LanguageMode> parse_language(const std::string& value) {
  if (value == "auto") return LanguageMode::autoDetect;
  if (value == "en") return LanguageMode::english;
  if (value == "zh") return LanguageMode::chinese;
  return std::nullopt;
}

#if defined(_WIN32)
std::uint64_t filetime_to_u64(const FILETIME& ft) {
  ULARGE_INTEGER u{};
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  return u.QuadPart;
}

std::wstring widen(const std::string& value) {
  if (value.empty()) return {};
  const int len = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
  std::wstring out(static_cast<std::size_t>(len), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, out.data(), len);
  if (!out.empty() && out.back() == L'\0') out.pop_back();
  return out;
}

std::string narrow(const std::wstring& value) {
  if (value.empty()) return {};
  const int len = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<std::size_t>(len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), len, nullptr, nullptr);
  if (!out.empty() && out.back() == '\0') out.pop_back();
  return out;
}

BOOL CALLBACK close_window_for_pid(HWND hwnd, LPARAM lparam) {
  DWORD windowPid = 0;
  GetWindowThreadProcessId(hwnd, &windowPid);
  if (windowPid == static_cast<DWORD>(lparam) && IsWindowVisible(hwnd)) {
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
  }
  return TRUE;
}
#endif

}  // namespace

std::filesystem::path default_data_dir() {
#if defined(_WIN32)
  PWSTR appData = nullptr;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &appData))) {
    std::filesystem::path path(appData);
    CoTaskMemFree(appData);
    return path / "SteamVR Bail Out";
  }
#endif
  return std::filesystem::current_path() / ".local" / "steamvr-bail-out";
}

std::filesystem::path default_settings_path() { return default_data_dir() / "settings.json"; }

std::filesystem::path default_log_path() { return default_data_dir() / "diagnostic.log"; }

SettingsStore::SettingsStore(std::filesystem::path path) : path_(std::move(path)) {}

SettingsLoadResult SettingsStore::load() {
  std::ifstream in(path_);
  if (!in) {
    SettingsLoadResult result{default_settings(), true, "missing_recovered"};
    save(result.settings);
    return result;
  }
  std::stringstream buffer;
  buffer << in.rdbuf();
  auto result = parse_settings_json(buffer.str());
  if (result.recovered) save(result.settings);
  return result;
}

bool SettingsStore::save(const Settings& settings) {
  std::error_code ec;
  std::filesystem::create_directories(path_.parent_path(), ec);
  std::ofstream out(path_, std::ios::trunc);
  if (!out) return false;
  out << settings_to_json(settings);
  return static_cast<bool>(out);
}

std::string settings_to_json(const Settings& settings) {
  std::ostringstream out;
  out << "{\n"
      << "  \"schemaVersion\": " << settings.schemaVersion << ",\n"
      << "  \"configuredHand\": \"" << hand_to_json(settings.configuredHand) << "\",\n"
      << "  \"exitCountdownDurationSeconds\": " << settings.exitCountdownDurationSeconds << ",\n"
      << "  \"exitControlTimeoutSeconds\": " << settings.exitControlTimeoutSeconds << ",\n"
      << "  \"countdownSoundEnabled\": " << (settings.countdownSoundEnabled ? "true" : "false") << ",\n"
      << "  \"language\": \"" << language_to_json(settings.language) << "\"\n"
      << "}\n";
  return out.str();
}

SettingsLoadResult parse_settings_json(const std::string& text) {
  Settings settings;
  const auto schema = match_int(text, "schemaVersion");
  const auto handText = match_string(text, "configuredHand");
  const auto hand = handText ? parse_hand(*handText) : std::optional<Hand>{};
  const auto duration = match_int(text, "exitCountdownDurationSeconds");
  const auto timeout = match_int(text, "exitControlTimeoutSeconds");
  const auto sound = match_bool(text, "countdownSoundEnabled");
  const auto languageText = match_string(text, "language");
  const auto language = languageText ? parse_language(*languageText) : std::optional<LanguageMode>{};

  if (!schema || !hand || !duration || !timeout || !sound || !language) {
    return {default_settings(), true, "malformed_recovered"};
  }

  settings.schemaVersion = *schema;
  settings.configuredHand = *hand;
  settings.exitCountdownDurationSeconds = *duration;
  settings.exitControlTimeoutSeconds = *timeout;
  settings.countdownSoundEnabled = *sound;
  settings.language = *language;
  if (!validate_settings(settings)) {
    return {default_settings(), true, "invalid_recovered"};
  }
  return {settings, false, "loaded"};
}

std::string LanguageService::resolve(LanguageMode mode, const std::string& steamLanguage) {
  if (mode == LanguageMode::english) return "en";
  if (mode == LanguageMode::chinese) return "zh";
  if (steamLanguage.rfind("zh", 0) == 0 || steamLanguage == "schinese" || steamLanguage == "tchinese") return "zh";
  if (steamLanguage.rfind("en", 0) == 0) return "en";
#if defined(_WIN32)
  wchar_t localeName[LOCALE_NAME_MAX_LENGTH] = {};
  if (GetUserDefaultLocaleName(localeName, LOCALE_NAME_MAX_LENGTH) > 0) {
    const std::string os = narrow(localeName);
    if (os.rfind("zh", 0) == 0) return "zh";
  }
#endif
  return "en";
}

std::optional<ExitTargetIdentity> ProcessController::identity_from_pid(
    std::uint32_t pid, std::string appKey, std::string displayName) const {
#if defined(_WIN32)
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return std::nullopt;

  FILETIME create{}, exit{}, kernel{}, user{};
  if (!GetProcessTimes(process, &create, &exit, &kernel, &user)) {
    CloseHandle(process);
    return std::nullopt;
  }

  wchar_t path[MAX_PATH] = {};
  DWORD size = MAX_PATH;
  std::string exe;
  if (QueryFullProcessImageNameW(process, 0, path, &size)) exe = narrow(path);
  CloseHandle(process);
  if (exe.empty()) return std::nullopt;
  return ExitTargetIdentity{pid, filetime_to_u64(create), exe, std::move(appKey), std::move(displayName)};
#else
  (void)pid;
  (void)appKey;
  (void)displayName;
  return std::nullopt;
#endif
}

bool ProcessController::is_alive(const ExitTargetIdentity& target) const {
#if defined(_WIN32)
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target.pid);
  if (!process) return false;
  DWORD code = 0;
  const bool alive = GetExitCodeProcess(process, &code) && code == STILL_ACTIVE;
  CloseHandle(process);
  return alive;
#else
  (void)target;
  return false;
#endif
}

bool ProcessController::same_identity_alive(const ExitTargetIdentity& target) const {
#if defined(_WIN32)
  const auto current = identity_from_pid(target.pid, target.appKey, target.displayName);
  return current.has_value() && same_identity(*current, target) && is_alive(target);
#else
  (void)target;
  return false;
#endif
}

ProcessCloseResult ProcessController::request_graceful_close(const ExitTargetIdentity& target) const {
#if defined(_WIN32)
  if (!same_identity_alive(target)) return is_alive(target) ? ProcessCloseResult::identityMismatch : ProcessCloseResult::targetGone;
  EnumWindows(close_window_for_pid, static_cast<LPARAM>(target.pid));
  return ProcessCloseResult::gracefulRequested;
#else
  (void)target;
  return ProcessCloseResult::failed;
#endif
}

ProcessCloseResult ProcessController::force_close(const ExitTargetIdentity& target) const {
#if defined(_WIN32)
  if (!same_identity_alive(target)) return is_alive(target) ? ProcessCloseResult::identityMismatch : ProcessCloseResult::targetGone;
  HANDLE process = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, target.pid);
  if (!process) return ProcessCloseResult::failed;
  const bool ok = TerminateProcess(process, 1) != 0;
  CloseHandle(process);
  return ok ? ProcessCloseResult::forceClosed : ProcessCloseResult::failed;
#else
  (void)target;
  return ProcessCloseResult::failed;
#endif
}

}  // namespace sbo
