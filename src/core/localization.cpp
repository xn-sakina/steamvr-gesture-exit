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

constexpr std::array<TextPair, 71> strings{{
    {"SteamVR Gesture Exit", "SteamVR手势退出"},
    {"Exit game", "退出游戏"},
    {"Exit this game?", "要退出这个游戏吗？"},
    {"If the game won't close, it will be force closed.",
     "游戏关不掉时，会强制关闭。"},
    {"Confirm exit", "确认退出"},
    {"Dismiss", "关闭"},
    {"Cancel exit", "取消退出"},
    {"Exiting…", "正在退出…"},
    {"Exit failed", "退出失败"},
    {"Please close the game manually.", "请手动关闭游戏。"},
    {"Settings", "设置"},
    {"Wave with", "挥动哪只手柄"},
    {"Exit countdown", "退出倒计时"},
    {"Exit button display time", "退出按钮显示时间"},
    {"Countdown sound", "倒计时声音"},
    {"Language", "语言"},
    {"Close the SteamVR menu. Wave the selected controller side to side at chest height.",
     "关闭 SteamVR 菜单，在胸前左右挥动选定的手柄。"},
    {"Point at Exit with the other controller and pull the trigger.",
     "用另一只手柄指向退出按钮，扣动扳机。"},
    {"Confirm and wait for the countdown. The game may be force closed.",
     "确认后等待倒计时。游戏关不掉时会强制关闭。"},
    {"Running status", "运行状态"},
    {"Current game", "当前游戏"},
    {"Game to exit", "将退出的游戏"},
    {"Wave controller", "挥手手柄"},
    {"Other controller", "另一只手柄"},
    {"Controller recognized", "已识别到手柄"},
    {"Controller not recognized", "未识别到手柄"},
    {"Wave the controller side to side at chest height.", "把手柄抬到胸前左右挥动。"},
    {"Connect the selected controller.", "请连接选定的手柄。"},
    {"Move the controller until SteamVR can track it.",
     "移动手柄，让 SteamVR 重新识别。"},
    {"When Exit appears, point at it and pull the trigger.",
     "退出按钮出现后，指向按钮并扣动扳机。"},
    {"Connect the other controller.", "请连接另一只手柄。"},
    {"Move the other controller until SteamVR can track it.",
     "移动另一只手柄，让 SteamVR 重新识别。"},
    {"Last exit", "上次退出"},
    {"Available", "可用"},
    {"Unavailable", "不可用"},
    {"Never", "从未"},
    {"Using default settings.", "已使用默认设置。"},
    {"Couldn't save settings. Please try again.", "设置保存失败，请重试。"},
    {"How to use", "使用说明"},
    {"Left", "左手"},
    {"Right", "右手"},
    {"Auto", "自动"},
    {"English", "英文"},
    {"Chinese", "中文"},
    {"Found", "已找到"},
    {"Identifying the current game…", "正在识别当前游戏…"},
    {"Can't read game information from SteamVR.", "无法读取 SteamVR 的游戏信息。"},
    {"No VR game is open.", "当前没有打开 VR 游戏。"},
    {"SteamVR can't find the current game.", "SteamVR 未找到当前游戏。"},
    {"The current screen isn't a VR game.", "当前打开的不是 VR 游戏。"},
    {"SteamVR can't identify the current game.", "SteamVR 无法识别当前游戏。"},
    {"Can't read information about the current game.", "无法读取当前游戏信息。"},
    {"The current app isn't a game this tool can exit.",
     "当前打开的不是可退出的游戏。"},
    {"Can't determine which game to exit.", "无法确定要退出的游戏。"},
    {"The game has closed or can't be found.", "游戏已关闭或无法找到。"},
    {"No permission to close this game.", "没有权限关闭这个游戏。"},
    {"Can't confirm the game is still running.", "无法确认游戏是否仍在运行。"},
    {"Can't confirm the game program's location.", "无法确认游戏程序位置。"},
    {"This program can't be exited.", "这个程序不能退出。"},
    {"Close the SteamVR menu before waving.", "关闭 SteamVR 菜单后再挥手。"},
    {"Please wait", "请稍候"},
    {"SteamVR Gesture Exit", "SteamVR手势退出"},
    {"Wave to show Exit", "挥手唤出退出按钮"},
    {"Unknown game", "未知游戏"},
    {"s", "秒"},
    {"On", "开"},
    {"Off", "关"},
    {"Succeeded", "成功"},
    {"Cancelled", "已取消"},
    {"Failed", "失败"},
    {"Game closed or changed", "游戏已关闭或切换"},
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
