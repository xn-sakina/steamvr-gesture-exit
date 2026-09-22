#include "core/localization.hpp"

#include <array>
#include <cctype>
#include <string>

namespace bailout::core {
namespace {

struct TextPair { std::string_view english; std::string_view chinese; };

constexpr std::array<TextPair, 52> strings{{
    {"SteamVR Bail Out", "SteamVR 告别退出"},
    {"Exit game", "退出游戏"},
    {"Exit this game?", "要退出这个游戏吗？"},
    {"The game may be force closed if it does not respond.", "如果游戏没有响应，可能会被强制关闭。"},
    {"Confirm exit", "确认退出"}, {"Dismiss", "关闭"}, {"Cancel exit", "取消退出"},
    {"Exiting…", "正在退出…"}, {"Exit failed", "退出失败"},
    {"Please close the game manually.", "请手动关闭游戏。"}, {"Settings", "设置"},
    {"Configured hand", "设定手"}, {"Exit countdown", "退出倒计时"},
    {"Control timeout", "退出控件超时"}, {"Countdown sound", "倒计时声音"},
    {"Language", "语言"},
    {"Wave outward with your configured hand to show the exit control.", "用设定手向外挥动以显示退出控件。"},
    {"Point and confirm with the opposite controller.", "使用另一只控制器指向并确认。"},
    {"An unresponsive game may be force closed after five seconds.", "无响应的游戏可能会在五秒后被强制关闭。"},
    {"Diagnostics", "诊断"}, {"Current VR game", "当前 VR 游戏"},
    {"Exit target", "退出目标"}, {"Configured hand tracking", "设定手跟踪"},
    {"Opposite hand pointer", "另一只手的指针"}, {"Last exit", "上次退出"},
    {"Available", "可用"}, {"Unavailable", "不可用"}, {"Never", "从未"},
    {"Settings were reset to safe defaults.", "设置已恢复为安全默认值。"},
    {"Quick guide", "快速指南"}, {"Left", "左手"}, {"Right", "右手"},
    {"Auto", "自动"}, {"English", "英文"}, {"Chinese", "中文"},
    {"Ready", "就绪"},
    {"OpenVR applications interface is unavailable.", "OpenVR 应用接口不可用。"},
    {"No VR scene application is running.", "当前没有运行 VR 场景应用。"},
    {"SteamVR did not provide a scene process.", "SteamVR 未提供场景进程。"},
    {"The add-on process cannot be a game.", "附加组件自身不能作为游戏。"},
    {"SteamVR could not map the scene process to an app key.", "SteamVR 无法将场景进程映射到应用键。"},
    {"Required SteamVR application properties could not be read.", "无法读取所需的 SteamVR 应用属性。"},
    {"The scene application was rejected by the game safety rules.", "场景应用未通过游戏安全规则。"},
    {"A Current VR Game could not be identified safely.", "无法安全识别当前 VR 游戏。"},
    {"The game process exited or cannot be opened.", "游戏进程已退出或无法打开。"},
    {"Access to the game process was denied.", "访问游戏进程被拒绝。"},
    {"The game process identity could not be read.", "无法读取游戏进程身份。"},
    {"The game executable path could not be read.", "无法读取游戏可执行文件路径。"},
    {"The mapped process was rejected by the safety denylist.", "映射的进程被安全拒绝列表拦截。"},
    {"Dashboard is open; gesture and exit controls are disabled.", "Dashboard 已打开；手势和退出控件已禁用。"},
}};

bool is_chinese(std::string_view tag) {
  std::string lower(tag);
  for (char& value : lower) value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
  return lower == "chinese" || lower.starts_with("zh") || lower.find("schinese") != std::string::npos;
}

}  // namespace

Language resolve_language(const Language configured, const std::string_view steam_language,
                          const std::string_view os_language) noexcept {
  if (configured != Language::Auto) return configured;
  if (!steam_language.empty()) return is_chinese(steam_language) ? Language::ChineseSimplified
                                                                 : Language::English;
  return is_chinese(os_language) ? Language::ChineseSimplified : Language::English;
}

std::string_view localize(const StringId id, const Language resolved_language) noexcept {
  const auto index = static_cast<std::size_t>(id);
  if (index >= strings.size()) return {};
  return resolved_language == Language::ChineseSimplified ? strings[index].chinese
                                                          : strings[index].english;
}

}  // namespace bailout::core
