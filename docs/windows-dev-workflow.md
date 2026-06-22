# Windows 编译自测全流程

本文以 `docs/prd/mvp.md` 的 MVP 为准，目标是让你在 Windows 上从编译、注册 SteamVR Add-on、运行自测、改代码后重测，到取消注册都能用同一个脚本完成。

## 前置条件

- Windows 10/11。
- 已安装 CMake。
- 已安装 Visual Studio 2022 C++ 构建工具，或能提供 `Visual Studio 17 2022` 生成器。
- 已安装 SteamVR。
- 仓库内已有 `openvr/`，或用 `-OpenVrRoot` 指向另一个 OpenVR SDK 根目录。该目录必须包含：
  - `headers/openvr.h`
  - `lib/win64/openvr_api.lib`
  - `bin/win64/openvr_api.dll`

建议在 PowerShell 里从仓库根目录执行以下命令。

## 一键编译、测试、打包

```powershell
.\scripts\windows\dev.ps1 -Action rebuild
```

这会：

1. 停止旧的 `steamvr-bail-out.exe`。
2. 配置 CMake 到 `.build\windows`。
3. 编译 runtime。
4. 生成 `.build\windows\package`。
5. 运行 core tests。

如果你的 OpenVR SDK 不在仓库内：

```powershell
.\scripts\windows\dev.ps1 -Action rebuild -OpenVrRoot "D:\SDK\openvr"
```

## 注册 SteamVR Add-on

首次自测或 manifest 改动后运行：

```powershell
.\scripts\windows\dev.ps1 -Action register
```

如果脚本找不到 SteamVR：

```powershell
.\scripts\windows\dev.ps1 -Action register -SteamVrRoot "C:\Program Files (x86)\Steam\steamapps\common\SteamVR"
```

注册的是：

```text
.build\windows\package\steamvr-bail-out.vrmanifest
```

注册后启动或重启 SteamVR，在 SteamVR Dashboard 里应能看到 `SteamVR Bail Out`。

## 启动 runtime

```powershell
.\scripts\windows\dev.ps1 -Action start
```

或编译后自动启动：

```powershell
.\scripts\windows\dev.ps1 -Action rebuild -Run
```

SteamVR 不会热重载正在运行的 exe。每次改 C++ 代码后，使用：

```powershell
.\scripts\windows\dev.ps1 -Action rebuild -Run
```

脚本会先停掉旧进程，再编译、测试、打包并启动新 exe。通常不需要重启 SteamVR。

## 什么时候需要重启 SteamVR

只改 C++ 逻辑、UI 绘制、日志、设置、手势、进程关闭逻辑：

```powershell
.\scripts\windows\dev.ps1 -Action rebuild -Run
```

改了 manifest、app key、Dashboard overlay 注册信息、包路径：

```powershell
.\scripts\windows\dev.ps1 -Action rebuild -Register -Run
```

然后重启 SteamVR。

## VR 内 MVP 自测顺序

1. 启动 SteamVR。
2. 启动 `steamvr-bail-out.exe`。
3. 打开 SteamVR Dashboard，确认 `SteamVR Bail Out` 面板可见。
4. 在 Dashboard 面板里切换 Configured Hand、Countdown、Timeout、Sound、Language，确认设置立即保存。
5. 不启动游戏时挥手，确认不显示 Exit Control，Dashboard diagnostics 说明不可用原因。
6. 启动 VRChat，等 Dashboard diagnostics 显示 Current VR Game 和 Exit Target 可用。
7. 用 Configured Hand 做告别挥手，确认手腕附近出现 Exit Control。
8. 用 Opposite Hand Pointer hover/click，确认有 hover 反馈并进入二次确认。
9. 点击 Dismiss，确认控制消失。
10. 再次挥手，点击 Confirm，确认 HUD 倒计时出现，Cancel 只在手腕 Exit Control 上可用。
11. 倒计时期间点击 Cancel，确认游戏不退出，Last Exit 为 canceled。
12. 再次 Confirm，不取消，确认倒计时结束后只关闭 VRChat 主进程，SteamVR 和 add-on 仍然运行。
13. 至少再用两个 SteamVR 游戏做 smoke test：能安全退出则记录 success；不能识别 Exit Target 时必须拒绝显示控制，并在 Dashboard diagnostics 解释原因。

## 查看状态和日志

```powershell
.\scripts\windows\dev.ps1 -Action status
.\scripts\windows\dev.ps1 -Action logs
```

日志位置：

```text
%APPDATA%\SteamVR Bail Out\diagnostic.log
```

日志不应包含连续 tracking samples、游戏内容、聊天、音频或宽泛进程 dump。

## 取消注册 SteamVR Add-on

```powershell
.\scripts\windows\dev.ps1 -Action unregister
```

如果取消注册后 SteamVR Dashboard 里仍能看到旧入口，重启 SteamVR。

## 常用命令速查

```powershell
# 只配置
.\scripts\windows\dev.ps1 -Action configure

# 只编译 runtime
.\scripts\windows\dev.ps1 -Action build

# 只跑 tests
.\scripts\windows\dev.ps1 -Action test

# 打包
.\scripts\windows\dev.ps1 -Action package

# 停止旧 runtime
.\scripts\windows\dev.ps1 -Action stop

# 编译、测试、打包、启动
.\scripts\windows\dev.ps1 -Action rebuild -Run

# 编译、测试、打包、注册、启动
.\scripts\windows\dev.ps1 -Action rebuild -Register -Run

# 编译、打包、启动，跳过 core tests
.\scripts\windows\dev.ps1 -Action rebuild -Run -NoTest
```
