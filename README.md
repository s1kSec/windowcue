# WindowCue

WindowCue 是 Windows 窗口快捷键工具：在实时预览列表中选择窗口，为多个窗口分别绑定全局快捷键，快速恢复、切换或最小化它们。

当前发布采用 `WindowCue.Native` 原生 C++/Win32 实现，提供中文和英文独立版本。C/C++ 运行时静态链接，图标及主题资源嵌入 EXE，无需安装 .NET 或 VC++ Redistributable。

## 功能

- 显示应用图标与 DWM 实时窗口预览，包括后台及最小化窗口。
- 为多个窗口分别绑定快捷键，已绑定卡片以浅绿色边框标记。
- 目标已在前台时，快捷键将其最小化；否则恢复并切到目标窗口。
- 默认使用 **Alt+P** 显示或隐藏 WindowCue，可在自身卡片中修改或取消。
- 默认启动后静默驻留托盘；首次正常运行启用当前用户登录时自启动，可在齿轮设置中自由开关。
- 支持明暗主题、列表滚动及高 DPI；亮色底色为 `#f5f8fc`，关闭或最小化后驻留系统托盘。
- 标题栏主题按钮左侧提供 SVG 设置齿轮，设置中的 GitHub 图标打开项目仓库。
- 绑定保存到本机；目标窗口关闭后，对应绑定会失效并被移除。

## 下载与安装

系统要求：**Windows 10 1809（10.0.17763）及以上 x64，或 Windows 11 x64**。这是当前发布基线，与 MSIX 清单一致。主题图标使用 Windows Direct2D 原生 SVG 支持，不能按“所有 Windows 10 版本”理解兼容范围；参见 [Microsoft SVG 支持说明](https://learn.microsoft.com/en-us/windows/win32/direct2d/svg-support)。

从 [GitHub Releases](https://github.com/s1kSec/windowcue/releases) 下载所需语言的 x64 便携版 ZIP，解压到可保留的位置，双击其中的 `WindowCue.exe`。ZIP 内包含运行文件和隐私文档；校验值见对应 Release 的说明及 `SHA256SUMS.txt`。便携版无需安装，设置仍默认保存到当前用户的本地应用数据目录。

Microsoft Store 渠道正在准备：中文正式身份 MSIX 已可构建，**尚未上架或通过商店认证**；英文独立商店产品仍待补齐官方身份。GitHub Release 可附带明确标注的中文未签名 Store 提交包，供开发者提交 Partner Center 使用；普通用户使用便携 ZIP。未签名 MSIX 不能直接作为用户安装包，本地测试签名包和证书不随 Release 发布。

## 2026-10-13：1.0.2.0 更新

新增齿轮设置、开机自启动开关和 GitHub 项目入口；亮色底色调整为 `#f5f8fc`。便携版使用当前用户 `Run` 启动项；MSIX 使用 Windows `StartupTask`，清单默认启用，首次激活后由 Windows 管理。两种发行方式均静默启动到托盘，保留用户关闭和 Windows 禁用状态。Windows 已禁用时，设置提供系统“启动应用”入口；应用不会覆盖系统的禁用选择。

## 2026-10-12：1.0.1.0 修复

预览列表按实际客户区宽度计算列数和所需高度，在达到屏幕工作区允许的最大高度前展示完整卡片；达到上限后才滚动。DWM 预览在源窗口尺寸变化及 WindowCue 失去焦点时重新提交缩略图属性，修复前台切换后的预览边界更新。

中文、英文原生编译及桌面 UI 回归均通过，覆盖模拟 DPI、列表高度与命中、真实 DWM 源窗口缩放和快捷键前台切换。用户截图中的 ChatGPT 像素溢出尚未用真实应用复现；本次验证范围见 [原生文档](WindowCue.Native/README.md)。已构建中文正式身份的 `1.0.1.0` MSIX，详情见 [打包验证记录](packaging/VALIDATION.md)。本次只更新源码和 Store 构建产物，没有发布新的 GitHub 便携版 Release。

## 使用

1. 启动中文或英文版本后，按 **Alt+P** 或双击托盘图标打开界面，再在预览列表中选择目标窗口。也可使用 `WindowCue.exe --show` 直接打开。
2. 点击快捷键输入框，按下带修饰键的组合，例如 `Ctrl+Alt+G`，然后点击“绑定”（Bind）。
3. 继续选择其他窗口并绑定不同组合；快捷键已被占用时，换用其他组合。
4. 按已绑定快捷键恢复或最小化对应窗口。选择已绑定卡片并点击“取消绑定”（Unbind）可解除绑定。

左上角 WindowCue 自身卡片控制唤出快捷键。首次启动默认注册 `Alt+P`；已有自定义绑定优先，手动取消后不会在重启时自动恢复默认绑定。默认组合已被其他程序占用时，可双击托盘图标打开界面并改绑。

点击标题栏太阳/月亮按钮切换主题；左侧齿轮打开设置，提供自启动开关及 GitHub 图标。首次正常运行默认启用随当前用户登录自启动，开关可关闭或重新开启；如果在 Windows 中禁用，请通过设置提供的入口在系统“启动应用”中重新启用。MSIX 使用包启动任务，不依赖版本化 WindowsApps 中的 EXE 路径。便携版启用后请保留解压目录，移动后从新位置运行会更新仍启用的启动路径；删除前建议先关闭自启动。曾手动放入 `shell:startup` 的旧快捷方式需自行移除，以免形成额外启动来源。

关闭或最小化只会隐藏到托盘，快捷键仍可使用；双击托盘图标恢复界面，右键托盘图标选择“退出”才会结束程序。同一配置目录已有实例时，重复启动静默退出；`--show` 仅用于新实例显式显示界面。已有实例请使用快捷键或托盘图标打开。

默认设置目录为 `%LOCALAPPDATA%\WindowCue\`，包含 `bindings.json`、主题偏好、自身快捷键初始化标记及便携版 `startup-preference.txt`。绑定对应当前窗口；关闭目标应用后，需为新打开的窗口重新绑定。窗口标题和预览可能包含敏感信息，详细数据处理说明见 [隐私政策](https://github.com/s1kSec/windowcue/blob/main/PRIVACY.md)。

## 从源码构建

在 Windows x64 上安装 **Visual Studio 2022 C++ x64 Build Tools** 和 **Windows 10/11 SDK**。从仓库根目录运行 PowerShell：

```powershell
# 中文、英文原生 Release EXE
& .\WindowCue.Native\build.ps1

# GitHub 便携版 ZIP
& .\release\Build-Portable.ps1 -RunSelfTests -RunStartupTest

# 中文 Microsoft Store 未签名 MSIX；沿用正式身份及当前版本
& .\packaging\Build-Msix.ps1 -Language CN -Mode Store -RunSelfTests
```

原生 EXE 输出到 `dist\WindowCue.Native\`；便携 ZIP、校验值和构建报告输出到 `dist\release\<运行标识>\`；MSIX 输出到 `packaging\output\`。当前源码及打包版本为 `1.0.2.0`，便携构建和 MSIX 构建共用 `packaging/partner-center.json` 的版本配置。MSIX 打包说明、身份配置、隔离测试及认证边界见 [packaging/README.md](packaging/README.md)。原生实现及回归程序构建见 [WindowCue.Native/README.md](WindowCue.Native/README.md)。

## 仓库与反馈

| 路径 | 内容 |
| --- | --- |
| `WindowCue.Native/` | 当前发布的原生源码、资源与构建脚本 |
| `packaging/` | Microsoft Store MSIX 清单、图标、身份配置与打包工具 |
| `release/` | GitHub 独立便携版构建工具 |
| `WindowCue/` | 保留的旧 C#/.NET 8 WPF 实现，运行需 .NET 8 Desktop Runtime |
| `CodexDetachedChatHotkey.cs`、根 `build.ps1` | 保留的独立聊天快捷键工具，使用说明见 [原工具文档](docs/CodexDetachedChatHotkey.md) |

反馈问题或建议请提交 [GitHub Issues](https://github.com/s1kSec/windowcue/issues)。隐私政策同时覆盖当前原生 EXE 和由 `packaging/` 构建的 MSIX，详见 [PRIVACY.md](PRIVACY.md) 与 [隐私审计记录](PRIVACY-AUDIT.md)。

本仓库目前**未提供 LICENSE**；公开源码不代表已授予某种开源许可证，后续许可由项目作者决定。
