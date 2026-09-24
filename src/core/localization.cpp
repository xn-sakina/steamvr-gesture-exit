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
    {"SteamVR Bail Out", "SteamVR 告别退出"},
    {"Exit game", "退出游戏"},
    {"Exit this game?", "要退出这个游戏吗？"},
    {"The game may be force closed if it does not respond.",
     "如果游戏没有响应，可能会被强制关闭。"},
    {"Confirm exit", "确认退出"},
    {"Dismiss", "关闭"},
    {"Cancel exit", "取消退出"},
    {"Exiting…", "正在退出…"},
    {"Exit failed", "退出失败"},
    {"Please close the game manually.", "请手动关闭游戏。"},
    {"Settings", "设置"},
    {"Wave with", "挥手用哪只手"},
    {"Exit countdown", "退出倒计时"},
    {"Control timeout", "退出控件超时"},
    {"Countdown sound", "倒计时声音"},
    {"Language", "语言"},
    {"Close Dashboard, then raise the selected controller to chest height and wave side to side.",
     "关闭 Dashboard 后，把挥手手柄抬到胸前左右挥动，显示退出按钮。"},
    {"Point at the exit button and pull the trigger with the other controller.",
     "用另一只手柄指向退出按钮，按扳机确认。"},
    {"An unresponsive game may be force closed after five seconds.",
     "无响应的游戏可能会在五秒后被强制关闭。"},
    {"Diagnostics", "诊断"},
    {"Current VR game", "当前 VR 游戏"},
    {"Exit target", "退出目标"},
    {"Wave controller", "挥手手柄"},
    {"Click controller", "点击手柄"},
    {"Position OK", "位置正常"},
    {"Position lost", "找不到位置"},
    {"Position only; raise to chest height and wave side to side.",
     "仅表示位置正常；抬到胸前左右挥动。"},
    {"Connect the controller selected under Wave with.", "请连接上方选择的挥手手柄。"},
    {"Move the wave controller into tracking range.", "请把挥手手柄移到追踪范围内。"},
    {"When the exit button appears, point and pull the trigger.",
     "退出按钮出现后，用这只手柄指向并按扳机。"},
    {"Connect the other controller to click the exit button.",
     "请连接另一只手柄，用它点击退出按钮。"},
    {"Move the click controller into tracking range.", "请把点击手柄移到追踪范围内。"},
    {"Last exit", "上次退出"},
    {"Available", "可用"},
    {"Unavailable", "不可用"},
    {"Never", "从未"},
    {"Settings were reset to safe defaults.", "设置已恢复为安全默认值。"},
    {"Could not save settings. Change was not applied.", "无法保存设置，修改未生效。"},
    {"Quick guide", "快速指南"},
    {"Left", "左手"},
    {"Right", "右手"},
    {"Auto", "自动"},
    {"English", "英文"},
    {"Chinese", "中文"},
    {"Ready", "就绪"},
    {"Identifying the current game…", "正在识别当前游戏…"},
    {"OpenVR applications interface is unavailable.", "OpenVR 应用接口不可用。"},
    {"No VR scene application is running.", "当前没有运行 VR 场景应用。"},
    {"SteamVR did not provide a scene process.", "SteamVR 未提供场景进程。"},
    {"The add-on process cannot be a game.", "附加组件自身不能作为游戏。"},
    {"SteamVR could not map the scene process to an app key.",
     "SteamVR 无法将场景进程映射到应用键。"},
    {"Required SteamVR application properties could not be read.",
     "无法读取所需的 SteamVR 应用属性。"},
    {"The scene application was rejected by the game safety rules.",
     "场景应用未通过游戏安全规则。"},
    {"A Current VR Game could not be identified safely.", "无法安全识别当前 VR 游戏。"},
    {"The game process exited or cannot be opened.", "游戏进程已退出或无法打开。"},
    {"Access to the game process was denied.", "访问游戏进程被拒绝。"},
    {"The game process identity could not be read.", "无法读取游戏进程身份。"},
    {"The game executable path could not be read.", "无法读取游戏可执行文件路径。"},
    {"The mapped process was rejected by the safety denylist.", "映射的进程被安全拒绝列表拦截。"},
    {"Dashboard is open; gesture and exit controls are disabled.",
     "Dashboard 已打开；手势和退出控件已禁用。"},
    {"Please wait", "请稍候"},
    {"Bail Out", "安全退场"},
    {"Safe VR game exit", "安全退出 VR 游戏"},
    {"game", "游戏"},
    {"s", "秒"},
    {"On", "开"},
    {"Off", "关"},
    {"Succeeded", "成功"},
    {"Cancelled", "已取消"},
    {"Failed", "失败"},
    {"Target unavailable", "目标不可用"},
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
