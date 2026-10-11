# Codex 独立聊天全局快捷键

这个小工具把 `Alt+X` 绑定到以下准确流程：

1. 枚举 `ChatGPT.exe` 的顶层 HWND，识别 Quick Chat 的独立窗口。
2. 如果目标已经位于前台，按 `Alt+X` 会最小化它；否则调用
   `ShowWindowAsync(SW_RESTORE)`、`SetWindowPos(HWND_TOP)`、`BringWindowToTop`
   和 `SetForegroundWindow`，必要时用 `AttachThreadInput` 再尝试获取焦点。
3. 只有目标窗口尚不存在时，才发送 `Ctrl+Alt+N`，并通过 Windows UI Automation
   调用“在新窗口中打开”按钮来创建它。

它不读取聊天内容、不调用 OpenAI API，也不使用屏幕坐标。

## 使用

如果旧版程序仍在运行，请先右键它的托盘图标并退出，避免旧版继续占用 `Alt+X`。

然后双击：

`CodexDetachedChatHotkey-v3.exe`

程序常驻系统托盘。按 `Alt+X` 显示或最小化独立聊天窗口；右键托盘图标可退出。

## 开机启动（可选）

按 `Win+R`，输入 `shell:startup`，把 `CodexDetachedChatHotkey-v3.exe` 的快捷方式放入打开的文件夹。

## 重新编译

在 PowerShell 中运行：

```powershell
.\build.ps1
```

它只使用 Windows 自带的 .NET Framework C# 编译器，不下载依赖。

## 排错

日志位于：

`%LOCALAPPDATA%\CodexDetachedChatHotkey\hotkey.log`

识别或创建失败时程序不会弹出系统告警，错误详情只记录到该日志。

如果提示找不到“在新窗口中打开”按钮，请确认：

- Codex 正常运行；
- `Ctrl+Alt+N` 能显示侧边 Quick Chat；
- Codex 界面语言为中文、繁体中文、英文、日文或韩文之一。

工具重启后也会重新枚举 HWND，因此图二窗口已经存在时不会再新建一个窗口。
