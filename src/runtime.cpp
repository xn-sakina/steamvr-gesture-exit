#include "runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <thread>

#if !defined(SBO_WITH_OPENVR)

namespace sbo {

std::vector<std::uint8_t> render_overlay_rgba(const RenderModel&, int width, int height, bool) {
  return std::vector<std::uint8_t>(static_cast<std::size_t>(width * height * 4), 0);
}

struct RuntimeSession::Impl {
  std::atomic<bool> stop = false;
};

RuntimeSession::RuntimeSession() : impl_(std::make_unique<Impl>()) {}
RuntimeSession::~RuntimeSession() = default;

int RuntimeSession::run() {
  std::cerr << "SteamVR Bail Out runtime requires a Windows build with SBO_WITH_OPENVR.\n";
  return 1;
}

void RuntimeSession::request_stop() { impl_->stop = true; }

}  // namespace sbo

#else

#include "openvr.h"

#if defined(_WIN32)
#include <Windows.h>
#include <mmsystem.h>
#endif

namespace sbo {

namespace {

constexpr const char* kAppKey = "io.github.steamvr-bail-out";
constexpr int kOverlayWidth = 640;
constexpr int kOverlayHeight = 360;

struct OverlayHandles {
  vr::VROverlayHandle_t dashboard = vr::k_ulOverlayHandleInvalid;
  vr::VROverlayHandle_t dashboardThumb = vr::k_ulOverlayHandleInvalid;
  vr::VROverlayHandle_t exitControl = vr::k_ulOverlayHandleInvalid;
  vr::VROverlayHandle_t hud = vr::k_ulOverlayHandleInvalid;
};

struct Pixel {
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
  std::uint8_t a;
};

enum class DashboardRegion { none, hand, duration, timeout, sound, language };

#if defined(_WIN32)
std::wstring widen_utf8(const std::string& value) {
  if (value.empty()) return {};
  const int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0);
  if (len <= 0) return {};
  std::wstring out(static_cast<std::size_t>(len), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, out.data(), len);
  if (!out.empty() && out.back() == L'\0') out.pop_back();
  return out;
}

void draw_system_text(std::vector<std::uint8_t>& img, int w, int h, int x, int y, const std::string& text, Pixel color) {
  const auto wide = widen_utf8(text);
  if (wide.empty()) return;

  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = w;
  info.bmiHeader.biHeight = -h;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  void* bits = nullptr;
  HDC screen = GetDC(nullptr);
  HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, screen);
  if (!bitmap || !bits) return;

  auto* bgra = static_cast<std::uint8_t*>(bits);
  std::vector<std::uint8_t> before(static_cast<std::size_t>(w * h * 4));
  for (int i = 0; i < w * h; ++i) {
    const auto src = static_cast<std::size_t>(i * 4);
    bgra[src + 0] = img[src + 2];
    bgra[src + 1] = img[src + 1];
    bgra[src + 2] = img[src + 0];
    bgra[src + 3] = img[src + 3];
    before[src + 0] = bgra[src + 0];
    before[src + 1] = bgra[src + 1];
    before[src + 2] = bgra[src + 2];
    before[src + 3] = bgra[src + 3];
  }

  HDC dc = CreateCompatibleDC(nullptr);
  HGDIOBJ oldBitmap = SelectObject(dc, bitmap);
  HFONT font = CreateFontW(22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  HGDIOBJ oldFont = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(color.r, color.g, color.b));
  RECT rect{x, y, w - 18, h};
  DrawTextW(dc, wide.c_str(), -1, &rect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

  for (int i = 0; i < w * h; ++i) {
    const auto src = static_cast<std::size_t>(i * 4);
    if (bgra[src + 0] != before[src + 0] || bgra[src + 1] != before[src + 1] || bgra[src + 2] != before[src + 2]) {
      img[src + 0] = bgra[src + 2];
      img[src + 1] = bgra[src + 1];
      img[src + 2] = bgra[src + 0];
      img[src + 3] = std::max(img[src + 3], color.a);
    }
  }

  SelectObject(dc, oldFont);
  SelectObject(dc, oldBitmap);
  DeleteObject(font);
  DeleteObject(bitmap);
  DeleteDC(dc);
}
#endif

void fill_rect(std::vector<std::uint8_t>& img, int w, int h, int x, int y, int rw, int rh, Pixel color) {
  for (int yy = std::max(0, y); yy < std::min(h, y + rh); ++yy) {
    for (int xx = std::max(0, x); xx < std::min(w, x + rw); ++xx) {
      const auto i = static_cast<std::size_t>((yy * w + xx) * 4);
      img[i + 0] = color.r;
      img[i + 1] = color.g;
      img[i + 2] = color.b;
      img[i + 3] = color.a;
    }
  }
}

std::array<std::uint8_t, 7> glyph_bits(char ch) {
  switch (static_cast<char>(std::toupper(static_cast<unsigned char>(ch)))) {
    case 'A': return {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    case 'B': return {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
    case 'C': return {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
    case 'D': return {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
    case 'E': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    case 'F': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
    case 'G': return {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F};
    case 'H': return {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    case 'I': return {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
    case 'J': return {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C};
    case 'K': return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    case 'L': return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    case 'M': return {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
    case 'N': return {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    case 'O': return {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    case 'P': return {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    case 'Q': return {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
    case 'R': return {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    case 'S': return {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
    case 'T': return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    case 'U': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    case 'V': return {0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04};
    case 'W': return {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11};
    case 'X': return {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
    case 'Y': return {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
    case 'Z': return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
    case '0': return {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    case '1': return {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
    case '2': return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
    case '3': return {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
    case '4': return {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
    case '5': return {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
    case '6': return {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E};
    case '7': return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    case '8': return {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
    case '9': return {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E};
    case ':': return {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
    case '.': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
    case ',': return {0x00, 0x00, 0x00, 0x00, 0x04, 0x04, 0x08};
    case '-': return {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
    case '_': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F};
    case '=': return {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00};
    case '/': return {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
    case '(': return {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02};
    case ')': return {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08};
    default: return {0x1F, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
  }
}

void draw_bar_text(std::vector<std::uint8_t>& img, int w, int h, int x, int y, const std::string& text, Pixel color) {
#if defined(_WIN32)
  draw_system_text(img, w, h, x, y, text, color);
#else
  int cursor = x;
  constexpr int scale = 2;
  for (unsigned char ch : text) {
    if (ch == ' ') {
      cursor += 4 * scale;
      continue;
    }
    const auto bits = glyph_bits(static_cast<char>(ch));
    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        if ((bits[row] & (1 << (4 - col))) != 0) {
          fill_rect(img, w, h, cursor + col * scale, y + row * scale, scale, scale, color);
        }
      }
    }
    cursor += 6 * scale;
    if (cursor > w - 20) break;
  }
#endif
}

bool zh_ui(const RenderModel& model) {
  return model.languageCode == "zh";
}

std::string ui_text(const RenderModel& model, const char* en, const char* zh) {
  return zh_ui(model) ? std::string(zh) : std::string(en);
}

std::string localized_title(const RenderModel& model) {
  switch (model.state) {
    case FlowState::idleVisible: return ui_text(model, "Bail Out", "退出控制");
    case FlowState::confirming: return ui_text(model, "Confirm Game Exit", "确认退出游戏");
    case FlowState::countingDown: return ui_text(model, "Exiting in", "即将退出");
    case FlowState::exiting: return ui_text(model, "Exiting...", "正在退出...");
    case FlowState::failureNotice: return ui_text(model, "Exit failed", "退出失败");
    case FlowState::none: break;
  }
  return model.title.empty() ? ui_text(model, "Bail Out", "退出控制") : model.title;
}

std::string localized_body(const RenderModel& model) {
  switch (model.state) {
    case FlowState::idleVisible: return ui_text(model, "Point with opposite hand to confirm", "用另一只手指向并确认");
    case FlowState::confirming: return ui_text(model, "May force close if unresponsive", "如果游戏无响应，可能会被强制关闭");
    case FlowState::countingDown: return ui_text(model, "Cancel from wrist control", "可从手腕控制取消退出");
    case FlowState::exiting: return ui_text(model, "Closing confirmed game", "正在关闭已确认的游戏");
    case FlowState::failureNotice: return ui_text(model, "Close the game manually", "请手动关闭游戏");
    case FlowState::none: break;
  }
  return model.body;
}

vr::HmdMatrix34_t identity_transform(float x, float y, float z) {
  vr::HmdMatrix34_t m{};
  m.m[0][0] = 1.0f;
  m.m[1][1] = 1.0f;
  m.m[2][2] = 1.0f;
  m.m[0][3] = x;
  m.m[1][3] = y;
  m.m[2][3] = z;
  return m;
}

vr::HmdMatrix34_t multiply_transform(const vr::HmdMatrix34_t& a, const vr::HmdMatrix34_t& b) {
  vr::HmdMatrix34_t out{};
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < 3; ++col) {
      out.m[row][col] = a.m[row][0] * b.m[0][col] + a.m[row][1] * b.m[1][col] + a.m[row][2] * b.m[2][col];
    }
    out.m[row][3] = a.m[row][0] * b.m[0][3] + a.m[row][1] * b.m[1][3] + a.m[row][2] * b.m[2][3] + a.m[row][3];
  }
  return out;
}

vr::HmdMatrix34_t smooth_overlay_transform(
    const vr::HmdMatrix34_t& previous,
    const vr::HmdMatrix34_t& target,
    double alpha) {
  vr::HmdMatrix34_t out = target;
  const auto blend = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
  for (int row = 0; row < 3; ++row) {
    out.m[row][3] = previous.m[row][3] + (target.m[row][3] - previous.m[row][3]) * blend;
  }
  return out;
}

std::string app_property_string(const char* appKey, vr::EVRApplicationProperty property) {
  if (!vr::VRApplications()) return {};
  char buffer[1024] = {};
  vr::EVRApplicationError error = vr::VRApplicationError_None;
  vr::VRApplications()->GetApplicationPropertyString(appKey, property, buffer, sizeof(buffer), &error);
  if (error != vr::VRApplicationError_None) return {};
  return buffer;
}

bool app_property_bool(const char* appKey, vr::EVRApplicationProperty property) {
  if (!vr::VRApplications() || appKey == nullptr || std::strlen(appKey) == 0) return false;
  vr::EVRApplicationError error = vr::VRApplicationError_None;
  const bool value = vr::VRApplications()->GetApplicationPropertyBool(appKey, property, &error);
  return error == vr::VRApplicationError_None && value;
}

std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::optional<std::string> app_key_for_pid(std::uint32_t pid) {
  if (!vr::VRApplications() || pid == 0) return std::nullopt;
  char keyBuffer[vr::k_unMaxApplicationKeyLength] = {};
  const auto error = vr::VRApplications()->GetApplicationKeyByProcessId(pid, keyBuffer, sizeof(keyBuffer));
  if (error != vr::VRApplicationError_None || keyBuffer[0] == '\0') return std::nullopt;
  return std::string(keyBuffer);
}

bool is_filtered_app_key(const std::string& appKey) {
  const auto key = lower_ascii(appKey);
  static constexpr std::array<const char*, 10> denied = {
      "steam.overlay",
      "steam.dashboard",
      "steamvr",
      "openvr.tool",
      "system.generated",
      "openvr.tool.steamvr_room_setup",
      "openvr.tool.steamvr_environments",
      "openvr.tool.steamvr",
      "steam.app.steamvr",
      kAppKey};
  return std::any_of(denied.begin(), denied.end(), [&](const char* deniedKey) {
    const std::string deniedLower = lower_ascii(deniedKey);
    return key == deniedLower || key.rfind(deniedLower + ".", 0) == 0;
  });
}

bool starts_with(const std::string& value, const char* prefix) {
  return value.rfind(prefix, 0) == 0;
}

bool is_supported_game_candidate(const std::string& appKey) {
  // For MVP, accept Steam scene applications only. Unknown scene apps fail
  // closed until we can classify them from stronger SteamVR metadata.
  return starts_with(lower_ascii(appKey), "steam.app.");
}

bool is_denied_runtime_executable(const std::string& path) {
  const auto file = lower_ascii(std::filesystem::path(path).filename().string());
  static constexpr std::array<const char*, 9> denied = {
      "steam.exe", "steamwebhelper.exe", "vrserver.exe", "vrmonitor.exe", "vrcompositor.exe",
      "vrdashboard.exe", "vrstartup.exe", "vrwebhelper.exe", "steamtours.exe"};
  return std::any_of(denied.begin(), denied.end(), [&](const char* name) { return file == name; });
}

const char* yes_no(bool value) {
  return value ? "yes" : "no";
}

std::string hand_name(Hand hand) {
  return hand == Hand::left ? "left" : "right";
}

std::string language_mode_name(LanguageMode language) {
  switch (language) {
    case LanguageMode::autoDetect: return "auto";
    case LanguageMode::english: return "en";
    case LanguageMode::chinese: return "zh";
  }
  return "auto";
}

std::string steamvr_language() {
  if (!vr::VRSettings()) return {};
  char buffer[128] = {};
  vr::EVRSettingsError error = vr::VRSettingsError_None;
  vr::VRSettings()->GetString(vr::k_pch_SteamVR_Section, "language", buffer, sizeof(buffer), &error);
  return error == vr::VRSettingsError_None ? std::string(buffer) : std::string{};
}

Settings next_dashboard_setting(Settings settings, DashboardRegion region) {
  if (region == DashboardRegion::hand) {
    settings.configuredHand = settings.configuredHand == Hand::left ? Hand::right : Hand::left;
  } else if (region == DashboardRegion::duration) {
    static constexpr std::array<int, 5> durations = {1, 3, 5, 10, 15};
    const auto it = std::find(durations.begin(), durations.end(), settings.exitCountdownDurationSeconds);
    settings.exitCountdownDurationSeconds =
        it == durations.end() || std::next(it) == durations.end() ? durations.front() : *std::next(it);
  } else if (region == DashboardRegion::timeout) {
    static constexpr std::array<int, 5> timeouts = {3, 5, 8, 12, 30};
    const auto it = std::find(timeouts.begin(), timeouts.end(), settings.exitControlTimeoutSeconds);
    settings.exitControlTimeoutSeconds =
        it == timeouts.end() || std::next(it) == timeouts.end() ? timeouts.front() : *std::next(it);
  } else if (region == DashboardRegion::sound) {
    settings.countdownSoundEnabled = !settings.countdownSoundEnabled;
  } else if (region == DashboardRegion::language) {
    if (settings.language == LanguageMode::autoDetect) settings.language = LanguageMode::english;
    else if (settings.language == LanguageMode::english) settings.language = LanguageMode::chinese;
    else settings.language = LanguageMode::autoDetect;
  }
  return settings;
}

}  // namespace

std::vector<std::uint8_t> render_overlay_rgba(const RenderModel& model, int width, int height, bool dashboard) {
  std::vector<std::uint8_t> image(static_cast<std::size_t>(width * height * 4), 0);
  const Pixel bg = dashboard ? Pixel{18, 22, 30, 235} : Pixel{12, 14, 18, 220};
  const Pixel panel = Pixel{36, 45, 60, 245};
  const Pixel text = Pixel{230, 238, 245, 255};
  const Pixel accent = Pixel{68, 170, 255, 255};
  const Pixel warn = Pixel{255, 190, 70, 255};
  const Pixel danger = Pixel{255, 88, 88, 255};

  fill_rect(image, width, height, 0, 0, width, height, bg);
  fill_rect(image, width, height, 18, 18, width - 36, height - 36, panel);

  if (dashboard) {
    draw_bar_text(image, width, height, 42, 42, "SteamVR Bail Out", text);
    draw_bar_text(image, width, height, 42, 70, ui_text(model, "Settings: ", "设置: ") + model.body, accent);
    fill_rect(image, width, height, 42, 102, 105, 32, Pixel{42, 76, 116, 255});
    fill_rect(image, width, height, 152, 102, 105, 32, Pixel{42, 76, 116, 255});
    fill_rect(image, width, height, 262, 102, 105, 32, Pixel{42, 76, 116, 255});
    fill_rect(image, width, height, 372, 102, 105, 32, Pixel{42, 76, 116, 255});
    fill_rect(image, width, height, 482, 102, 105, 32, Pixel{42, 76, 116, 255});
    draw_bar_text(image, width, height, 60, 112, ui_text(model, "HAND", "手"), text);
    draw_bar_text(image, width, height, 164, 112, ui_text(model, "DURATION", "时长"), text);
    draw_bar_text(image, width, height, 280, 112, ui_text(model, "TIMEOUT", "超时"), text);
    draw_bar_text(image, width, height, 396, 112, ui_text(model, "SOUND", "声音"), text);
    draw_bar_text(image, width, height, 500, 112, ui_text(model, "LANG", "语言"), text);
    draw_bar_text(image, width, height, 42, 146,
                  ui_text(model, "Help: wave configured hand to show wrist control", "帮助: 挥动已配置手显示手腕控制"), text);
    draw_bar_text(image, width, height, 42, 174,
                  ui_text(model, "Confirm/cancel with opposite pointer only", "只能用另一只手指针确认或取消"), text);
    draw_bar_text(image, width, height, 42, 202,
                  ui_text(model, "Countdown then closes game; may force close", "倒计时后关闭游戏；无响应时可能强制关闭"), warn);
    draw_bar_text(image, width, height, 42, 230,
                  ui_text(model, "Safety: SteamVR is not closed by this add-on", "安全: 本插件不会关闭 SteamVR"), warn);
    draw_bar_text(image, width, height, 42, 258, ui_text(model, "Game: ", "游戏: ") + model.currentGameStatus, text);
    draw_bar_text(image, width, height, 42, 286, ui_text(model, "Target: ", "目标: ") + model.exitTargetStatus, text);
    draw_bar_text(image, width, height, 42, 314, ui_text(model, "Input: ", "输入: ") + model.controllerStatus, text);
    draw_bar_text(image, width, height, 42, 340,
                  ui_text(model, "Last: ", "上次退出: ") + std::string(to_string(model.lastExit)) +
                      ui_text(model, " Settings: ", " 设置: ") + model.settingsStatus,
                  text);
    return image;
  }

  draw_bar_text(image, width, height, 42, 54, localized_title(model), text);
  if (!model.targetName.empty()) draw_bar_text(image, width, height, 42, 96, model.targetName, accent);
  draw_bar_text(image, width, height, 42, 136, localized_body(model), warn);

  if (model.state == FlowState::confirming) {
    fill_rect(image, width, height, 70, 230, 190, 70, model.hover == HoverRegion::confirm ? danger : Pixel{130, 40, 40, 255});
    fill_rect(image, width, height, 380, 230, 190, 70, model.hover == HoverRegion::dismiss ? accent : Pixel{40, 78, 120, 255});
    draw_bar_text(image, width, height, 100, 252, ui_text(model, "CONFIRM", "确认"), text);
    draw_bar_text(image, width, height, 420, 252, ui_text(model, "DISMISS", "关闭"), text);
  } else if (model.state == FlowState::countingDown) {
    const int barWidth = static_cast<int>((width - 100) * std::clamp(model.progress, 0.0, 1.0));
    fill_rect(image, width, height, 50, 228, width - 100, 28, Pixel{70, 70, 80, 255});
    fill_rect(image, width, height, 50, 228, barWidth, 28, accent);
    draw_bar_text(image, width, height, 50, 270,
                  std::to_string(model.secondsRemaining) + ui_text(model, " seconds", " 秒"), text);
    if (model.cancelVisible) {
      fill_rect(image, width, height, 380, 280, 190, 54, model.hover == HoverRegion::cancel ? danger : Pixel{120, 40, 40, 255});
      draw_bar_text(image, width, height, 420, 298, ui_text(model, "CANCEL", "取消"), text);
    }
  } else if (model.state == FlowState::failureNotice) {
    fill_rect(image, width, height, 50, 230, width - 100, 64, danger);
    draw_bar_text(image, width, height, 90, 252, ui_text(model, "Close manually", "请手动关闭游戏"), text);
  } else if (model.state == FlowState::idleVisible) {
    fill_rect(image, width, height, 80, 230, 200, 70, model.hover == HoverRegion::primary ? accent : Pixel{40, 78, 120, 255});
    fill_rect(image, width, height, 390, 230, 160, 70, model.hover == HoverRegion::dismiss ? warn : Pixel{78, 70, 40, 255});
    draw_bar_text(image, width, height, 110, 252, ui_text(model, "OPEN", "打开"), text);
    draw_bar_text(image, width, height, 420, 252, ui_text(model, "DISMISS", "关闭"), text);
  }

  return image;
}

struct RuntimeSession::Impl {
  SettingsStore settingsStore;
  Settings settings;
  DiagnosticLog log;
  ProcessController process;
  FlowController flow;
  GestureRecognizer gesture;
  OverlayHandles overlays;
  vr::IVRSystem* system = nullptr;
  std::atomic<bool> stop = false;
  bool gracefulRequested = false;
  bool forceRequested = false;
  FlowController::TimePoint graceDue{};
  std::string lastAppKey;
  std::string currentGameStatus = "unavailable:no_current_game";
  std::string exitTargetStatus = "unavailable:no_current_game";
  std::string controllerStatus = "unknown";
  std::string settingsStatus = "not_loaded";
  std::string resolvedLanguage = "en";
  std::string lastControllerSetupStatus;
  vr::TrackedDeviceIndex_t lastConfiguredDevice = vr::k_unTrackedDeviceIndexInvalid;
  vr::TrackedDeviceIndex_t lastOppositeDevice = vr::k_unTrackedDeviceIndexInvalid;
  vr::HmdMatrix34_t lastConfiguredPose{};
  bool lastConfiguredPoseValid = false;
  vr::HmdMatrix34_t smoothedExitControlTransform{};
  bool smoothedExitControlTransformValid = false;
  vr::TrackedDeviceIndex_t smoothedExitControlDevice = vr::k_unTrackedDeviceIndexInvalid;
  FlowController::TimePoint lastExitControlSmoothing{};
  HoverRegion pressedRegion = HoverRegion::none;

  Impl() : settingsStore(default_settings_path()), log(default_log_path()) {
    const auto loaded = settingsStore.load();
    settings = loaded.settings;
    settingsStatus = loaded.status;
    flow.update_settings(settings, FlowController::Clock::now());
    log.write("settings_load", loaded.status);
    if (loaded.recovered) log.write("settings_recovered", loaded.status);
  }

  bool init_openvr() {
    vr::EVRInitError error = vr::VRInitError_None;
    system = vr::VR_Init(&error, vr::VRApplication_Overlay);
    if (error != vr::VRInitError_None || system == nullptr) {
      log.write("openvr_init_failed", vr::VR_GetVRInitErrorAsEnglishDescription(error));
      return false;
    }
    log.write("openvr_init_ok");

    if (!vr::VROverlay()) {
      log.write("overlay_init_failed", "null_overlay");
      return false;
    }

    auto overlay = vr::VROverlay();
    auto e = overlay->CreateDashboardOverlay(kAppKey, "SteamVR Bail Out", &overlays.dashboard, &overlays.dashboardThumb);
    if (e != vr::VROverlayError_None) {
      log.write("dashboard_overlay_failed", overlay->GetOverlayErrorNameFromEnum(e));
      return false;
    }
    e = overlay->CreateOverlay("io.github.steamvr-bail-out.exit-control", "SteamVR Bail Out Exit Control", &overlays.exitControl);
    if (e != vr::VROverlayError_None) {
      log.write("exit_overlay_failed", overlay->GetOverlayErrorNameFromEnum(e));
      return false;
    }
    e = overlay->CreateOverlay("io.github.steamvr-bail-out.hud", "SteamVR Bail Out HUD", &overlays.hud);
    if (e != vr::VROverlayError_None) {
      log.write("hud_overlay_failed", overlay->GetOverlayErrorNameFromEnum(e));
      return false;
    }

    overlay->SetOverlayWidthInMeters(overlays.dashboard, 1.6f);
    overlay->SetOverlayInputMethod(overlays.dashboard, vr::VROverlayInputMethod_Mouse);
    overlay->SetOverlayWidthInMeters(overlays.exitControl, 0.28f);
    overlay->SetOverlayInputMethod(overlays.exitControl, vr::VROverlayInputMethod_Mouse);
    overlay->SetOverlayFlag(overlays.exitControl, vr::VROverlayFlags_EnableClickStabilization, true);
    overlay->SetOverlayWidthInMeters(overlays.hud, 0.42f);
    overlay->SetOverlayInputMethod(overlays.hud, vr::VROverlayInputMethod_None);
    overlay->SetOverlayFlag(overlays.hud, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, false);

    render_dashboard();
    render_current();
    log.write("overlay_init_ok");
    return true;
  }

  void shutdown() {
    if (vr::VROverlay()) {
      if (overlays.exitControl != vr::k_ulOverlayHandleInvalid) vr::VROverlay()->DestroyOverlay(overlays.exitControl);
      if (overlays.hud != vr::k_ulOverlayHandleInvalid) vr::VROverlay()->DestroyOverlay(overlays.hud);
    }
    if (system) {
      vr::VR_Shutdown();
      system = nullptr;
    }
  }

  void run_loop() {
    log.write("addon_start");
    auto lastRender = FlowController::Clock::now();
    while (!stop.load()) {
      const auto now = FlowController::Clock::now();
      poll_events(now);
      poll_poses(now);
      update_current_game(now);
      execute(flow.tick(now), now);
      if (now - lastRender > std::chrono::milliseconds(80)) {
        render_current();
        lastRender = now;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
  }

  void poll_events(FlowController::TimePoint now) {
    vr::VREvent_t event{};
    while (system && system->PollNextEvent(&event, sizeof(event))) {
      if (event.eventType == vr::VREvent_Quit) stop = true;
    }

    while (vr::VROverlay() && vr::VROverlay()->PollNextOverlayEvent(overlays.exitControl, &event, sizeof(event))) {
      if (!is_event_from_opposite_pointer(event)) continue;
      switch (event.eventType) {
        case vr::VREvent_MouseMove:
          flow.set_hover(region_for_mouse(event.data.mouse.x, event.data.mouse.y));
          break;
        case vr::VREvent_MouseButtonDown:
          if (event.data.mouse.button == vr::VRMouseButton_Left) {
            pressedRegion = region_for_mouse(event.data.mouse.x, event.data.mouse.y);
          }
          break;
        case vr::VREvent_MouseButtonUp:
          if (event.data.mouse.button == vr::VRMouseButton_Left) {
            const auto releasedRegion = region_for_mouse(event.data.mouse.x, event.data.mouse.y);
            if (pressedRegion == releasedRegion) handle_click(now, releasedRegion);
            pressedRegion = HoverRegion::none;
          }
          break;
        case vr::VREvent_OverlayHidden:
          flow.set_hover(HoverRegion::none);
          pressedRegion = HoverRegion::none;
          break;
        default:
          break;
      }
    }

    while (vr::VROverlay() && vr::VROverlay()->PollNextOverlayEvent(overlays.dashboard, &event, sizeof(event))) {
      if (event.eventType == vr::VREvent_MouseButtonUp) {
        update_dashboard_setting(now, region_for_dashboard_mouse(event.data.mouse.x, event.data.mouse.y));
      }
    }
  }

  HoverRegion region_for_mouse(float x, float y) const {
    if (x < 0.0f || x > 1.0f || y < 0.0f || y > 1.0f) return HoverRegion::none;
    const bool inActionBand = y >= 0.08f && y <= 0.42f;
    if (!inActionBand) return HoverRegion::none;
    if (flow.state() == FlowState::confirming) {
      if (x >= 0.10f && x <= 0.42f) return HoverRegion::confirm;
      if (x >= 0.58f && x <= 0.90f) return HoverRegion::dismiss;
    }
    if (flow.state() == FlowState::countingDown && x >= 0.58f && x <= 0.92f) return HoverRegion::cancel;
    if (flow.state() == FlowState::idleVisible) {
      if (x >= 0.12f && x <= 0.45f) return HoverRegion::primary;
      if (x >= 0.62f && x <= 0.88f) return HoverRegion::dismiss;
    }
    return HoverRegion::none;
  }

  DashboardRegion region_for_dashboard_mouse(float x, float y) const {
    if (x < 0.0f || x > 1.0f || y < 0.58f || y > 0.70f) return DashboardRegion::none;
    if (x >= 0.06f && x <= 0.23f) return DashboardRegion::hand;
    if (x >= 0.24f && x <= 0.40f) return DashboardRegion::duration;
    if (x >= 0.41f && x <= 0.58f) return DashboardRegion::timeout;
    if (x >= 0.59f && x <= 0.75f) return DashboardRegion::sound;
    if (x >= 0.76f && x <= 0.93f) return DashboardRegion::language;
    return DashboardRegion::none;
  }

  bool is_event_from_opposite_pointer(const vr::VREvent_t& event) const {
    return lastOppositeDevice != vr::k_unTrackedDeviceIndexInvalid && event.trackedDeviceIndex == lastOppositeDevice;
  }

  void update_dashboard_setting(FlowController::TimePoint now, DashboardRegion region) {
    if (region == DashboardRegion::none) return;
    const Settings next = next_dashboard_setting(settings, region);
    if (!validate_settings(next)) return;
    settings = next;
    flow.update_settings(settings, now);
    const bool saved = settingsStore.save(settings);
    settingsStatus = saved ? "saved" : "save_failed";
    log.write("settings_changed", settingsStatus);
    render_dashboard();
    render_current();
  }

  void handle_click(FlowController::TimePoint now, HoverRegion region) {
    if (region == HoverRegion::dismiss) execute(flow.dismiss_clicked(now), now);
    else if (region == HoverRegion::primary) execute(flow.primary_clicked(now), now);
    else if (region == HoverRegion::confirm) execute(flow.confirm_clicked(now, revalidate_current_target(now)), now);
    else if (region == HoverRegion::cancel) execute(flow.cancel_clicked(now), now);
  }

  std::optional<ExitTargetIdentity> lastResolvedTarget;

  std::optional<ExitTargetIdentity> revalidate_current_target(FlowController::TimePoint now) {
    update_current_game(now);
    if (!lastResolvedTarget || !process.same_identity_alive(*lastResolvedTarget)) {
      return std::nullopt;
    }
    return lastResolvedTarget;
  }

  bool exit_overlay_accepts_pointer_events() const {
    if (!vr::VROverlay() || overlays.exitControl == vr::k_ulOverlayHandleInvalid) return false;
    vr::VROverlayInputMethod method = vr::VROverlayInputMethod_None;
    const auto error = vr::VROverlay()->GetOverlayInputMethod(overlays.exitControl, &method);
    return error == vr::VROverlayError_None && method == vr::VROverlayInputMethod_Mouse;
  }

  void poll_poses(FlowController::TimePoint now) {
    if (!system) return;
    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
    system->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, 0.0f, poses, vr::k_unMaxTrackedDeviceCount);

    const auto configuredRole = settings.configuredHand == Hand::left ? vr::TrackedControllerRole_LeftHand : vr::TrackedControllerRole_RightHand;
    const auto oppositeRole = settings.configuredHand == Hand::left ? vr::TrackedControllerRole_RightHand : vr::TrackedControllerRole_LeftHand;
    const auto hand = system->GetTrackedDeviceIndexForControllerRole(configuredRole);
    const auto opposite = system->GetTrackedDeviceIndexForControllerRole(oppositeRole);
    lastConfiguredDevice = hand;
    lastOppositeDevice = opposite;

    Prerequisites prerequisites;
    prerequisites.configuredHandTracked = hand != vr::k_unTrackedDeviceIndexInvalid && poses[hand].bPoseIsValid;
    lastConfiguredPoseValid = prerequisites.configuredHandTracked && hand < vr::k_unMaxTrackedDeviceCount;
    if (lastConfiguredPoseValid) lastConfiguredPose = poses[hand].mDeviceToAbsoluteTracking;
    const bool steamvrInputAvailable = system->IsInputAvailable();
    const bool oppositeController =
        opposite != vr::k_unTrackedDeviceIndexInvalid &&
        system->IsTrackedDeviceConnected(opposite) &&
        system->GetTrackedDeviceClass(opposite) == vr::TrackedDeviceClass_Controller;
    vr::VRControllerState_t oppositeState{};
    const bool oppositeInputReadable =
        oppositeController && system->GetControllerState(opposite, &oppositeState, sizeof(oppositeState));
    const bool overlayPointerEventsReady = exit_overlay_accepts_pointer_events();
    const bool oppositePoseValid = oppositeController && poses[opposite].bPoseIsValid;
    prerequisites.oppositePointerAvailable =
        steamvrInputAvailable && oppositePoseValid && oppositeInputReadable && overlayPointerEventsReady;
    const std::string pointerReason =
        prerequisites.oppositePointerAvailable ? "opposite_pointer_ok" :
        !steamvrInputAvailable ? "steamvr_input_unavailable" :
        !oppositeController ? "opposite_controller_unavailable" :
        !oppositePoseValid ? "opposite_pose_lost" :
        !oppositeInputReadable ? "opposite_input_unreadable" :
        !overlayPointerEventsReady ? "exit_overlay_input_unavailable" :
        "opposite_pointer_lost";
    controllerStatus = std::string(prerequisites.configuredHandTracked ? "hand_ok" : "hand_lost") + "," +
                       pointerReason +
                       ",steamvr_input=" + yes_no(steamvrInputAvailable) +
                       ",overlay_mouse=" + yes_no(overlayPointerEventsReady);
    const std::string setupStatus =
        std::string(prerequisites.configuredHandTracked ? "configured_hand_tracked" : "configured_hand_lost") + "," +
        pointerReason;
    if (setupStatus != lastControllerSetupStatus) {
      log.write("controller_setup_changed", setupStatus);
      lastControllerSetupStatus = setupStatus;
    }
    if (lastResolvedTarget) {
      prerequisites.currentGameAvailable = true;
      prerequisites.exitTargetAvailable = true;
      prerequisites.exitTarget = lastResolvedTarget;
      prerequisites.unavailableReason = "available";
    } else {
      prerequisites.unavailableReason = exitTargetStatus;
    }
    flow.update_prerequisites(prerequisites, now);

    if (prerequisites.configuredHandTracked && hand < vr::k_unMaxTrackedDeviceCount) {
	    const auto& m = poses[hand].mDeviceToAbsoluteTracking;
	    const auto& hmd = poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
      PoseSample sample;
      sample.time = now;
      sample.valid = true;
      sample.headsetPosition = poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid
                                   ? Vec3{hmd.m[0][3], hmd.m[1][3], hmd.m[2][3]}
                                   : Vec3{0.0, 1.6, 0.0};
	      sample.handPosition = {m.m[0][3], m.m[1][3], m.m[2][3]};
	      // OpenVR poses are absolute-space matrices. Use the HMD right and
	      // forward axes so the gesture means "side-to-side relative to the
	      // user" instead of "along world X". The controller X axis is the MVP
	      // palm-out approximation, and controller Y is the non-downward guard.
	      sample.palmDirection = {m.m[0][0], m.m[1][0], m.m[2][0]};
	      sample.handUp = {m.m[0][1], m.m[1][1], m.m[2][1]};
	      if (poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid) {
	        sample.headsetRight = {hmd.m[0][0], hmd.m[1][0], hmd.m[2][0]};
	        sample.headsetForward = {-hmd.m[0][2], -hmd.m[1][2], -hmd.m[2][2]};
	      }
	      if (gesture.add_sample(sample)) {
        log.write("farewell_gesture", "recognized");
        execute(flow.farewell_gesture(now), now);
      }
    }
  }

  void update_current_game(FlowController::TimePoint) {
    if (!vr::VRApplications()) {
      clear_current_game("openvr_applications_unavailable");
      return;
    }
    const auto sceneState = vr::VRApplications()->GetSceneApplicationState();
    if (sceneState != vr::EVRSceneApplicationState_Running) {
      clear_current_game(vr::VRApplications()->GetSceneApplicationStateNameFromEnum(sceneState));
      return;
    }

    const auto pid = vr::VRApplications()->GetCurrentSceneProcessId();
    if (pid == 0) {
      clear_current_game("missing_scene_pid");
      return;
    }

    const auto maybeAppKey = app_key_for_pid(pid);
    if (!maybeAppKey) {
      clear_current_game("missing_app_key");
      return;
    }
    std::string appKey = *maybeAppKey;
    const auto displayName = app_property_string(appKey.c_str(), vr::VRApplicationProperty_Name_String);
    if (!is_supported_game_candidate(appKey)) {
      clear_current_game("unsupported_app:" + appKey);
      return;
    }
    if (vr::VRApplications()->GetApplicationProcessId(appKey.c_str()) != pid) {
      clear_current_game("scene_pid_mismatch");
      return;
    }
    if (is_filtered_app_key(appKey) ||
        app_property_bool(appKey.c_str(), vr::VRApplicationProperty_IsDashboardOverlay_Bool) ||
        app_property_bool(appKey.c_str(), vr::VRApplicationProperty_IsInternal_Bool) ||
        app_property_bool(appKey.c_str(), vr::VRApplicationProperty_IsHidden_Bool)) {
      clear_current_game("filtered_app:" + appKey);
      return;
    }

    auto identity = process.identity_from_pid(pid, std::move(appKey), displayName.empty() ? "Current VR Game" : displayName);
    if (!identity) {
      clear_current_game("process_identity_unavailable");
      return;
    }
    if (is_denied_runtime_executable(identity->executablePath)) {
      clear_current_game("filtered_process");
      return;
    }
    if (!lastResolvedTarget || !same_identity(*lastResolvedTarget, *identity)) {
      log.write("exit_target_available", "mapped", &*identity);
    }
    lastResolvedTarget = identity;
    currentGameStatus = identity->displayName + "(" + identity->appKey + ")";
    exitTargetStatus = "available:" + identity->displayName;
    lastAppKey = identity->appKey;
  }

  void clear_current_game(const std::string& reason) {
    if (lastResolvedTarget || exitTargetStatus != "unavailable:" + reason) {
      log.write("exit_target_unavailable", reason);
    }
    lastResolvedTarget.reset();
    currentGameStatus = "unavailable:" + reason;
    exitTargetStatus = "unavailable:" + reason;
  }

  void execute(const FlowResult& result, FlowController::TimePoint now) {
    for (const auto& command : result.commands) {
      if (command.kind == CommandKind::logStateTransition) {
        log.write("state_transition", command.code, command.target ? &*command.target : nullptr);
        if (command.code == "confirm_locked_target") {
          log.write("countdown_started", std::to_string(settings.exitCountdownDurationSeconds), command.target ? &*command.target : nullptr);
        } else if (command.code == "countdown_elapsed") {
          log.write("countdown_elapsed", {}, command.target ? &*command.target : nullptr);
        }
      }
      if (command.kind == CommandKind::updateLastExit) {
        log.write("last_exit", command.code, command.target ? &*command.target : nullptr);
        if (command.code == "canceled") {
          log.write("countdown_canceled", {}, command.target ? &*command.target : nullptr);
          log.write("exit_canceled", {}, command.target ? &*command.target : nullptr);
        } else if (command.code == "success_graceful" || command.code == "success_forced") {
          log.write("exit_success", command.code, command.target ? &*command.target : nullptr);
        } else if (command.code == "target_gone") {
          log.write("exit_target_gone", command.code, command.target ? &*command.target : nullptr);
        } else {
          log.write("exit_failed", command.code, command.target ? &*command.target : nullptr);
        }
      }
      if (command.kind == CommandKind::playCountdownTick) play_tick();
      if (command.kind == CommandKind::requestGracefulClose && command.target) {
        const auto closeResult = process.request_graceful_close(*command.target);
        log.write("graceful_close_request", std::to_string(static_cast<int>(closeResult)), &*command.target);
        log.write("graceful_close_result", std::to_string(static_cast<int>(closeResult)), &*command.target);
        if (closeResult == ProcessCloseResult::targetGone || closeResult == ProcessCloseResult::identityMismatch) {
          execute(flow.confirmed_target_gone_or_invalid(now), now);
        } else if (closeResult == ProcessCloseResult::failed) {
          execute(flow.force_close_failed(now, "graceful_close_failed"), now);
        } else {
          gracefulRequested = true;
          graceDue = now + std::chrono::seconds(5);
        }
      }
      if (command.kind == CommandKind::requestForceClose && command.target) {
        const auto closeResult = process.force_close(*command.target);
        log.write("force_close_request", std::to_string(static_cast<int>(closeResult)), &*command.target);
        log.write("force_close_result", std::to_string(static_cast<int>(closeResult)), &*command.target);
        if (closeResult == ProcessCloseResult::forceClosed) execute(flow.force_close_succeeded(now), now);
        else if (closeResult == ProcessCloseResult::targetGone) execute(flow.graceful_close_succeeded(now), now);
        else execute(flow.force_close_failed(now, "force_close_failed"), now);
      }
    }

    if (flow.state() == FlowState::countingDown && flow.locked_target() &&
        !process.same_identity_alive(*flow.locked_target())) {
      execute(flow.confirmed_target_gone_or_invalid(now), now);
      return;
    }

    if (gracefulRequested && flow.state() == FlowState::exiting) {
      if (flow.locked_target() && !process.same_identity_alive(*flow.locked_target())) {
        gracefulRequested = false;
        execute(flow.graceful_close_succeeded(now), now);
      } else if (now >= graceDue) {
        gracefulRequested = false;
        execute(flow.grace_period_elapsed(now), now);
      }
    }

    render_current();
  }

  void play_tick() {
#if defined(_WIN32)
    MessageBeep(MB_OK);
#endif
  }

  void render_dashboard() {
    if (!vr::VROverlay()) return;
    auto model = flow.render_model();
    model.currentGameStatus = currentGameStatus;
    model.exitTargetStatus = exitTargetStatus;
    model.controllerStatus = controllerStatus;
    model.settingsStatus = settingsStatus;
    resolvedLanguage = LanguageService::resolve(settings.language, steamvr_language());
    model.languageCode = resolvedLanguage;
    model.body = "hand=" + hand_name(settings.configuredHand) +
                 " countdown=" + std::to_string(settings.exitCountdownDurationSeconds) +
                 " timeout=" + std::to_string(settings.exitControlTimeoutSeconds) +
                 " sound=" + std::string(settings.countdownSoundEnabled ? "on" : "off") +
                 " lang=" + language_mode_name(settings.language);
    const auto img = render_overlay_rgba(model, kOverlayWidth, kOverlayHeight, true);
    vr::VROverlay()->SetOverlayRaw(overlays.dashboard, const_cast<std::uint8_t*>(img.data()), kOverlayWidth, kOverlayHeight, 4);
  }

  void render_current() {
    if (!vr::VROverlay()) return;
    const auto& model = flow.render_model();
    if (model.exitControlVisible) {
      const auto img = render_overlay_rgba(model, kOverlayWidth, kOverlayHeight, false);
      vr::VROverlay()->SetOverlayRaw(overlays.exitControl, const_cast<std::uint8_t*>(img.data()), kOverlayWidth, kOverlayHeight, 4);
      if (lastConfiguredPoseValid) {
        const auto wristOffset = identity_transform(settings.configuredHand == Hand::left ? -0.10f : 0.10f, -0.07f, -0.16f);
        const auto target = multiply_transform(lastConfiguredPose, wristOffset);
        const auto now = FlowController::Clock::now();
        if (!smoothedExitControlTransformValid || smoothedExitControlDevice != lastConfiguredDevice) {
          smoothedExitControlTransform = target;
          smoothedExitControlTransformValid = true;
          smoothedExitControlDevice = lastConfiguredDevice;
        } else {
          const auto elapsed = std::chrono::duration<double>(now - lastExitControlSmoothing).count();
          const double alpha = 1.0 - std::exp(-std::max(0.0, elapsed) * 18.0);
          smoothedExitControlTransform = smooth_overlay_transform(smoothedExitControlTransform, target, alpha);
        }
        lastExitControlSmoothing = now;
        vr::VROverlay()->SetOverlayTransformAbsolute(
            overlays.exitControl, vr::TrackingUniverseStanding, &smoothedExitControlTransform);
        vr::VROverlay()->ShowOverlay(overlays.exitControl);
      } else {
        const auto transform = identity_transform(settings.configuredHand == Hand::left ? -0.10f : 0.10f, -0.07f, -0.16f);
        vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(overlays.exitControl, configured_device(), &transform);
        vr::VROverlay()->ShowOverlay(overlays.exitControl);
      }
    } else {
      smoothedExitControlTransformValid = false;
      smoothedExitControlDevice = vr::k_unTrackedDeviceIndexInvalid;
      vr::VROverlay()->HideOverlay(overlays.exitControl);
    }

    if (model.hudVisible) {
      const auto img = render_overlay_rgba(model, kOverlayWidth, kOverlayHeight, false);
      vr::VROverlay()->SetOverlayRaw(overlays.hud, const_cast<std::uint8_t*>(img.data()), kOverlayWidth, kOverlayHeight, 4);
      const auto transform = identity_transform(0.42f, 0.22f, -1.05f);
      vr::VROverlay()->SetOverlayTransformTrackedDeviceRelative(overlays.hud, vr::k_unTrackedDeviceIndex_Hmd, &transform);
      vr::VROverlay()->ShowOverlay(overlays.hud);
    } else {
      vr::VROverlay()->HideOverlay(overlays.hud);
    }
    render_dashboard();
  }

  vr::TrackedDeviceIndex_t configured_device() const {
    if (!system) return vr::k_unTrackedDeviceIndexInvalid;
    const auto role = settings.configuredHand == Hand::left ? vr::TrackedControllerRole_LeftHand : vr::TrackedControllerRole_RightHand;
    return system->GetTrackedDeviceIndexForControllerRole(role);
  }
};

RuntimeSession::RuntimeSession() : impl_(std::make_unique<Impl>()) {}

RuntimeSession::~RuntimeSession() = default;

int RuntimeSession::run() {
  if (!impl_->init_openvr()) return 1;
  impl_->run_loop();
  impl_->shutdown();
  return 0;
}

void RuntimeSession::request_stop() { impl_->stop = true; }

}  // namespace sbo

#endif
