# WindowCue.Native

这是 `WindowCue` 的独立原生 C++/Win32 分支。原有 `WindowCue` C#/.NET 目录和发布文件保持不变。

## 目标

- C++17 + Win32 + GDI + DWM Thumbnail API；
- 最低支持 Windows 10 x64；
- 不使用 Qt、MFC、.NET、WebView 或任何第三方运行库；
- 使用 `/MT` 静态链接 Microsoft C/C++ 运行时，发布结果为独立 EXE；
- 只依赖 Windows 自带的系统 DLL；
- 与现有版本共用 `%LocalAppData%\WindowCue\bindings.json`。

测试或便携场景可通过进程环境变量 `WINDOWCUE_SETTINGS_DIR` 覆盖配置目录；默认行为不变。

## 已映射功能

- 枚举正常、后台、最小化及 Win+D 后仍存在的 Alt+Tab 类顶级窗口；
- 过滤不可用的子窗口、工具窗口、NoActivate 窗口和系统辅助窗口；
- 解析打包应用的 `ApplicationFrameHost` 窗口；
- 原生应用图标与 DWM 实时窗口预览；
- 客户区左上角固定显示 WindowCue 自身卡片；自身卡片使用应用图标而非 DWM
  预览，并可绑定全局快捷键以从托盘恢复或再次隐藏；
- 默认使用 `Alt + P` 唤出或隐藏 WindowCue；已有自定义自身快捷键优先，
  手动取消后不会在重启时自动重新绑定；
- 前台时每 0.5 秒刷新窗口列表，DWM 预览约每 120ms 更新；
- 前台扫描结果未变化时不再重绘客户区，避免底板与 DWM 缩略图交替合成造成周期性闪烁；
- 窗口标题变化只局部刷新文字，已存在的 HWND 在列表刷新时复用原 DWM Thumbnail，
  不再因浏览器、终端或进度标题变化而整批注销和重建预览；
- 缩略图属性已做状态缓存，目标位置与可见性未变时不再向 DWM 重复提交相同参数；
- 前台切换、卡片悬停和选择只重绘装饰区；GDI 更新区域排除实时预览矩形，
  防止切到聊天窗口时重绘预览底色产生黑块或闪烁；
- 多窗口、多全局快捷键绑定；
- 持久化并验证精确目标 HWND，多个同进程、同类名且同标题的窗口在
  WindowCue 重启后不会互相串绑；目标已消失时不会回退绑定到另一个同名窗口；
- 与旧分支并行运行造成快捷键暂时被占用时保留配置并定时重试，不会误删绑定；
- 已绑定卡片浅绿色边框；
- 同一按钮在“绑定/取消绑定”之间切换；
- 录入快捷键时不显示等待提示，并暂时注销已有快捷键以避免误触发；
- 目标在前台时快捷键将其最小化，否则恢复、置顶并获取焦点；
- 目标 HWND 消失后自动注销并删除对应绑定；
- 自绘同色标题栏、内容驱动高度、最小预览区、固定底部绑定栏和内部滚动；
- 标题栏太阳/月亮按钮可切换暗色与亮色主题，选择保存在
  `%LocalAppData%\WindowCue\appearance.txt`；
- 主题图标作为 SVG 资源嵌入 EXE，并由 Windows Direct2D 原生渲染；
- 透明自绘标题使用 `DrawThemeTextEx` 的合成文本路径；最小化、最大化和关闭
  在亮暗主题下均仅由 DWM 绘制并负责命中、悬停和命令行为；
- 主题按钮依据 DWM 返回的真实标题按钮边界定位，在不同 DPI 下保持正确间距；
- 两套主题分别定义背景、卡片、边框、主次文字、输入框和按钮颜色，
  普通文字对比度不低于 4.5:1；
- 视觉层级采用 Codex/WorkBuddy 风格的中性灰、柔和灰白表面、黑白主按钮、
  细焦点边框和 8px 控件圆角；
- 中文界面使用 `Microsoft YaHei UI`、Natural ClearType 与较轻字重；
- DWM 统一提供整条标题栏的最终底色，客户区不再与其二次混色，并保留真实
  系统边框颜色，避免暗色标题栏分块和亮色顶部锯齿；
- 关闭及最小化进入系统托盘，双击托盘恢复，托盘菜单可退出；
- 中文、英文两个构建版本。

## 构建

需要 Microsoft Visual Studio 2022 C++ x64 Build Tools 和 Windows 10/11 SDK。构建工具只用于编译，不是程序运行依赖。

```powershell
.\build.ps1
```

输出：

```text
..\dist\WindowCue.Native\WindowCue_Native_CN.exe
..\dist\WindowCue.Native\WindowCue_Native_EN.exe
```

## Window chrome

- The normal window width is fixed at 1080 logical pixels (DPI-aware); edge resizing is disabled.
- Maximize and restore remain available from the custom caption.
- A subtle client-side frame remains visible without enabling the system resize border.
- Minimize, maximize/restore, and close are rendered by DWM itself. The custom frame
  delegates caption-button hit testing to `DwmDefWindowProc` and follows the selected theme.
