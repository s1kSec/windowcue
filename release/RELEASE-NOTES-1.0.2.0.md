# WindowCue 1.0.2.0

新增默认自启动、静默托盘启动和设置入口，同时支持 GitHub 便携版与 Microsoft Store MSIX 构建。

## 本次更新

- 主题按钮左侧增加 SVG 设置齿轮，包含开机自启动开关和 GitHub 项目图标。
- 首次正常运行默认启用当前用户登录自启动；关闭后重启仍保持关闭，Windows 用户或策略禁用状态优先。
- 启动后静默驻留托盘，按 **Alt+P** 或双击托盘图标打开；`WindowCue.exe --show` 也可打开界面。
- 同一配置目录已有实例时重复启动静默退出；`--show` 仅在新实例启动时显示界面。Explorer 重启后重建托盘图标。
- 亮色模式整体底色调整为 **`#f5f8fc`**。
- 保留 1.0.1.0 的预览列表高度、DWM 窗口缩放及前台切换修复。

## 下载与使用

Windows 10 1809（10.0.17763）及以上 x64，或 Windows 11 x64。下载对应语言的 ZIP，完整解压后运行 `WindowCue.exe`；原生运行时静态链接，无需另装 .NET 或 VC++ Redistributable。EXE 当前未签名。

默认设置仍保存到 `%LOCALAPPDATA%\WindowCue`，中文和英文版通常只需运行其中一种。首次启动后按 Alt+P 或双击托盘图标，点击设置齿轮开关自启动；Windows 已禁用时通过提供的系统启动设置入口重新启用。关闭或最小化隐藏到托盘；右键托盘“退出”结束程序。

便携版使用当前用户的 Run 启动项。启用期间保留解压目录，移动后从新位置运行会更新仍启用的启动路径，删除程序前建议先关闭自启动。曾手动放入 `shell:startup` 的旧快捷方式需自行移除。

## Microsoft Store 开发者提交附件

**`WindowCue-1.0.2.0-windows-x64-CN-Store-Submission-UNSIGNED.msix` 仅供开发者提交 Partner Center，未签名，不能直接安装。普通用户下载上面的便携 ZIP。**

该包沿用中文正式身份 `s1ksec.WindowCue` / PFN `s1ksec.WindowCue_14ska59yrcwza`。MSIX 通过 Windows StartupTask 管理自启动，首次激活后默认启用，不向版本化 WindowsApps 路径创建传统 Run 项。用户可在应用设置或 Windows 中关闭。

Microsoft Store **尚未上架或通过认证**；英文正式产品身份尚未提供，本 Release 不附 EN Store 包或本地测试签名包、证书。

## 校验与验证

| 文件 | SHA256 |
| --- | --- |
| `WindowCue-1.0.2.0-windows-x64-CN.zip` | `9F89504B52C0BB70BB52BCE1CE5F725EE645A338939ABFF432CAB5D98BECCBD1` |
| `WindowCue-1.0.2.0-windows-x64-EN.zip` | `C6A0E36D322AF85B4C5E76CB3A5CD747432C2B7347BEA82A9A7A502803EA1729` |
| `WindowCue-1.0.2.0-windows-x64-CN-Store-Submission-UNSIGNED.msix` | `B668997A88ADB05245D0D20EA78E8983FFAC6DFC7B0B38DAE85DE0B14E65AF69` |

同时提供 `SHA256SUMS.txt`，可用 PowerShell `Get-FileHash -Algorithm SHA256 <文件路径>` 核对。

中文、英文原生 Release/x64 编译与桌面 UI/DWM 回归均通过（CN=0、EN=0），覆盖设置齿轮位置、主题底色、开关有效状态、键盘操作和原有窗口预览/快捷键流程。两语言便携 ZIP 解压哈希一致，绑定/SVG/自启动自测均为 0；正常启动自行隐藏主窗口、消息响应、首次隔离自启动注册及重复启动静默退出均通过。中文正式身份 MSIX 的三项原生自测、MakeAppx 完整验证（未使用 /nv）、启动清单默认启用检查、PFN 与解包 EXE 哈希验证通过。运行依赖检查仅发现 Windows 系统 DLL/API-set。

验证使用独立配置目录及测试专用注册表子树，不修改真实用户的登录启动项。已安装 MSIX 的 WinRT 启动任务开关、实际注销/登录、Windows 10 实机及 WACK/Store 认证尚未验证；原生自测和 MakeAppx 验证不代表这些项目已通过。

[隐私政策](https://github.com/s1kSec/windowcue/blob/main/PRIVACY.md) · [反馈问题](https://github.com/s1kSec/windowcue/issues)

仓库当前未提供 LICENSE，未新增或选择开源许可证。
