#include "core/localization.hpp"

#include <array>
#include <cctype>
#include <string>

namespace bailout::core {
namespace {

struct TextPair {
  std::string_view english;
  std::string_view chinese;
};

constexpr std::array<TextPair, 58> strings{{
    {"SteamVR Gesture Exit", "SteamVR手势退出"},
    {"Exit", "退出"},
    {"Exit the game?", "是否退出？"},
    {"Confirm Exit", "确认退出"},
    {"Cancel", "取消"},
    {"Exit Failed", "退出失败"},
    {"Please close the game manually.", "请手动关闭游戏。"},
    {"Settings", "设置"},
    {"Controller to Wave", "挥动哪只手柄"},
    {"Exit Countdown", "退出倒计时"},
    {"Exit Button Display Time", "退出按钮显示时间"},
    {"Countdown Sound", "倒计时声音"},
    {"Language", "语言"},
    {"In a game, wave side to side as if saying goodbye.", "在游戏内，左右挥手，做出拜拜的手势。"},
    {"When the exit panel appears, use the other controller to press the Exit button.",
     "退出面板出现后，用另一只手柄按下退出按钮。"},
    {"The game will close when the countdown ends.", "倒计时结束后，游戏将会关闭。"},
    {"Running Status", "运行状态"},
    {"Current Game", "当前游戏"},
    {"Game to Exit", "将退出的游戏"},
    {"Controller to Wave", "挥手手柄"},
    {"Other Controller", "另一只手柄"},
    {"Controller Detected", "已识别到手柄"},
    {"Controller Not Detected", "未识别到手柄"},
    {"Last Exit", "上次退出"},
    {"Unavailable", "不可用"},
    {"Never", "从未"},
    {"Settings updated.", "配置已修改"},
    {"Failed to save settings. Please try again.", "配置保存失败，请重试。"},
    {"How to Use", "使用说明"},
    {"Left Hand", "左手"},
    {"Right Hand", "右手"},
    {"Auto", "自动"},
    {"English", "English"},
    {"Chinese", "中文"},
    {"Found", "已找到"},
    {"Identifying the current game…", "正在识别当前游戏…"},
    {"Unable to read the game currently running in SteamVR.", "无法读取 SteamVR 正在运行的游戏。"},
    {"No VR game is currently running.", "当前没有打开 VR 游戏。"},
    {"Current VR game not found.", "未找到当前 VR 游戏。"},
    {"The current app is not a VR game.", "当前打开的不是 VR 游戏。"},
    {"Unable to identify the current game.", "未识别当前游戏。"},
    {"Unable to read information about the current game.", "无法读取当前游戏信息。"},
    {"The current game is not supported.", "当前游戏不支持。"},
    {"No game detected.", "未识别到游戏。"},
    {"The game has closed or could not be found.", "游戏已关闭或无法找到。"},
    {"You do not have permission to close this game.", "没有权限关闭这个游戏程序。"},
    {"Unable to confirm whether the game is still running.", "无法确认游戏是否仍在运行。"},
    {"Unable to verify the game's location.", "无法确认游戏程序位置。"},
    {"This game cannot be exited.", "这个游戏程序无法退出。"},
    {"SteamVR Gesture Exit", "SteamVR手势退出"},
    {"Wave to Show the Exit Button", "挥手唤出退出按钮"},
    {"s", "秒"},
    {"On", "开"},
    {"Off", "关"},
    {"Succeeded", "成功"},
    {"Cancelled", "已取消"},
    {"Failed", "失败"},
    {"Game Closed or Switched", "游戏已关闭或切换"},
}};
static_assert(strings.size() == static_cast<std::size_t>(StringId::LastExitTargetUnavailable) + 1);

bool is_chinese(std::string_view tag) {
  std::string lower(tag);
  for (char& value : lower)
    value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
  return lower == "chinese" || lower.starts_with("zh") ||
         lower.find("schinese") != std::string::npos;
}

}  // namespace

Language resolve_language(const Language configured, const std::string_view steam_language,
                          const std::string_view os_language) noexcept {
  if (configured != Language::Auto)
    return configured;
  if (!os_language.empty())
    return is_chinese(os_language) ? Language::ChineseSimplified : Language::English;
  return is_chinese(steam_language) ? Language::ChineseSimplified : Language::English;
}

std::string_view localize(const StringId id, const Language resolved_language) noexcept {
  const auto index = static_cast<std::size_t>(id);
  if (index >= strings.size())
    return {};
  return resolved_language == Language::ChineseSimplified ? strings[index].chinese
                                                          : strings[index].english;
}

std::string_view localize_last_exit(const LastExitStatus status,
                                    const Language resolved_language) noexcept {
  switch (status) {
    case LastExitStatus::Never:
      return localize(StringId::Never, resolved_language);
    case LastExitStatus::Succeeded:
      return localize(StringId::LastExitSucceeded, resolved_language);
    case LastExitStatus::Cancelled:
      return localize(StringId::LastExitCancelled, resolved_language);
    case LastExitStatus::Failed:
      return localize(StringId::LastExitFailed, resolved_language);
    case LastExitStatus::TargetUnavailable:
      return localize(StringId::LastExitTargetUnavailable, resolved_language);
  }
  return {};
}

}  // namespace bailout::core
