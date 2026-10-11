# WindowCue 1.0.0.0

WindowCue 原生 Windows 窗口快捷键工具首次 GitHub 独立版发布，提供中文和英文两个 x64 便携包。

## 功能

- 显示应用图标及 DWM 实时窗口预览，为多个窗口分别绑定全局快捷键。
- 目标在前台时按快捷键最小化；否则恢复并切换到目标窗口。
- 默认 Alt+P 显示或隐藏 WindowCue，支持修改、取消自身绑定。
- 支持明暗主题、高 DPI、列表滚动及系统托盘；目标窗口关闭后清除失效绑定。

## 系统要求与安装

Windows 10 1809（10.0.17763）及以上 x64，或 Windows 11 x64。原生 C++ 运行时静态链接，无需另装 .NET 或 VC++ Redistributable。

下载所需语言的 ZIP，完整解压后双击 `WindowCue.exe`。选择目标卡片，录入带修饰键的组合并点击“绑定”。关闭或最小化会隐藏到托盘；双击托盘恢复，右键菜单选择“退出”结束程序。中文和英文版共用默认设置目录 `%LOCALAPPDATA%\WindowCue`，通常只需运行其中一种语言。

独立 EXE 当前未签名。ZIP 包含可执行文件、使用说明及原始隐私文档；“便携”表示无需安装，设置默认仍保存到用户的本地应用数据目录。

## 校验与验证

| 文件 | SHA256 |
| --- | --- |
| `WindowCue-1.0.0.0-windows-x64-CN.zip` | `1C799CEF37CCDA101309D6E126BEB7B3A4C479FAE5613A30B5A0EB09C5DE774D` |
| `WindowCue-1.0.0.0-windows-x64-EN.zip` | `EA5595A15C7897E152BE0AAFF0902CDDACD89AB9BA553DCB628A4842465663AF` |

同时提供 `SHA256SUMS.txt`，可用 PowerShell `Get-FileHash -Algorithm SHA256 <ZIP路径>` 核对。

两语言均在当前 Windows 11 x64 环境完成 Release 编译、PE 架构/版本及系统 DLL 依赖检查、ZIP 解压哈希验证、绑定/SVG 自测和隔离配置下的主窗口创建/消息响应检查。启动测试随后只终止新建测试进程；当前环境未能自动验证托盘菜单的正常退出，亦未执行 Windows 10 实机兼容性测试。

## Microsoft Store 与反馈

Microsoft Store 渠道仍在准备，尚未上架或完成商店认证。原 MSIX 流程、官方 Identity/Publisher 和 `1.0.0.0` 版本保持不变；中文 Store 未签名 MSIX 已重新构建、通过 MakeAppx 验证及解包检查。Store 未签名 MSIX、本地测试签名 MSIX 和证书均不作为本 Release 的用户安装包发布。

[隐私政策](https://github.com/s1kSec/windowcue/blob/main/PRIVACY.md) · [反馈问题](https://github.com/s1kSec/windowcue/issues)

仓库当前未提供 LICENSE，未新增或选择开源许可证。
