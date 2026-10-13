# WindowCue.Native

这是 `WindowCue` 的独立原生 C++/Win32 分支。原有 `WindowCue` C#/.NET 目录和发布文件保持不变。

## 目标

- C++17 + Win32 + GDI + DWM Thumbnail API；
- 最低支持 Windows 10 1809（17763）x64；
- 不使用 Qt、MFC、.NET、WebView 或任何第三方运行库；
- 使用 `/MT` 静态链接 Microsoft C/C++ 运行时，发布结果为独立 EXE；
- 只依赖 Windows 自带的系统 DLL；
- 与现有版本共用 `%LocalAppData%\WindowCue\bindings.json`。

测试可通过进程环境变量 `WINDOWCUE_SETTINGS_DIR` 覆盖配置目录；设置该变量时，自启动后端同时改用 `HKCU\Software\WindowCue\StartupTests\<目录哈希>` 的隔离注册表子树，绝不写入真实登录启动项，因此不应将该测试变量当作生产自启动的自定义配置方案。

## 2026-10-13：1.0.4.0 紧凑设置

- 普通状态的设置客户区收紧为 `300×106 DIP`，自启动开关为 `36×20 DIP`，以滑块位置与颜色表示状态，不显示“开启/关闭”文字。
- 移除“WindowCue 启动后静默驻留后台”说明；GitHub 使用 `20 DIP` 图标和透明点击区域，移除文字及底色按钮，点击直接打开项目仓库。
- Windows 禁用、策略限制或操作错误按需显示提示并扩展窗口，保留必要的系统设置入口。
- 延续键盘操作、焦点反馈、主题与高 DPI 缩放；默认静默托盘、便携版 Run 项及 MSIX StartupTask 后端保持既有行为。

## 2026-10-13：1.0.3.0 原生图标与标题栏交互

- 原创六齿圆角齿轮，统一两种主题的轮廓、线宽与中心孔，保留 `38×34 DIP` 的点击区域。
- SVG 渲染器将根节点宽高设为实际输出尺寸，保留 `viewBox`；修复 18px 裁切及高 DPI 图形未放大的问题。
- 月亮图标做光学居中，GitHub 图标保留安全内边距；主界面字体直接使用窗口 DPI。
- 悬停采用 125ms 缓出透明度过渡，按压下移 1 DIP，打开设置后保持选中；预乘 ARGB 和小范围绘制保留 DWM 玻璃及实时预览。
- 中英文原生工具提示提供设置快捷键和主题切换动作；动画在结束、隐藏或最小化时停止，并遵循 Windows 客户区动效设置。
- `--svg-self-test` 检查全部六个资源在 18/23/27/36px 下的实际 alpha 边缘及完整性，对称图标同时检查重心。

原生像素检查和真实标题栏视觉检查使用独立测试产物，未采集其他应用的窗口信息。100%、125%、150%、200% 像素渲染与模拟 DPI 字体检查不等于完成所有实际显示器组合验证。

```powershell
# 导出实际 Direct2D 像素、状态样本及几何统计
& .\WindowCue.Native\tests\Build-IconVisual.ps1 -Strict

# 在桌面上创建专用测试窗口，检查真实 DWM 与工具提示
& .\WindowCue.Native\tests\Build-CaptionVisual.ps1 -Language CN
& .\WindowCue.Native\tests\Build-CaptionVisual.ps1 -Language EN
```

本次视觉自检范围：

| 维度 | 检查与改进 |
| --- | --- |
| 排版 | 主界面与设置使用各自窗口 DPI；中英文字在真实 125% 桌面及模拟尺寸中可读 |
| 留白 | 齿轮轮廓与画布留白对称，GitHub 不贴边，月亮光学居中；保留完整点击区域 |
| 视觉层级 | 悬停、按压、设置打开与键盘焦点分别可辨，入口不抢占原生窗口控制 |
| 色彩 | 保留 `#f5f8fc` 亮色底色与暗色对比；检查实际 DWM 标题栏上的透明混色 |
| 动效 | 125ms 缓出淡入，中间帧无黑块；降低动效时立即切换，空闲停止计时器 |
| 微交互 | 实测自然悬停提示、轻微按压位移、圆角焦点环、Tab/Enter/Escape 与 Ctrl+, |
| 响应式 | 18/23/27/36px 真实渲染、100%–200% 模拟布局与字体高度检查 |
| 原创性 | 独立绘制六齿圆角轮廓，用原生像素比较轮廓、中心孔和视觉重量 |

专用测试不枚举其他应用、不加载用户绑定或注册启动项；测试窗口结束后恢复光标和前台。已验证的尺寸与当前桌面结果不能覆盖所有实际多屏组合。

## 2026-10-13：1.0.2.0 自启动与设置

- 标题栏主题按钮左侧增加内嵌 SVG 设置齿轮，提供自启动开关和 GitHub 项目入口。
- 正常启动静默进入托盘；`--startup` 同样静默，`--show` 仅在新实例启动时显式显示主窗口。同一配置目录已有实例时重复启动静默退出，可通过快捷键或托盘恢复。
- 便携版首次正常启动默认写入当前用户 `Run\WindowCue`，值为带引号的程序路径及 `--startup`；无需管理员权限。应用开关删除/重建自己的启动值，`startup-preference.txt` 记录选择。
- MSIX 使用包身份对应的 `Windows.ApplicationModel.StartupTask`；清单 `Enabled="true"`，首次激活后由 Windows 负责执行，应用通过 API 开关同一个 `WindowCueStartup`。
- Windows 已禁用或策略禁用时不会强行启用。Windows 用户禁用状态提供系统“启动应用”入口；便携版只读取 StartupApproved 状态，不修改该系统值。
- 亮色整体底色为 `#f5f8fc`；设置窗口使用所选主题。
- `--startup-self-test` 在隔离后端验证默认开启、关闭后不复活、重新开启、系统禁用及外部删除；构建脚本链接 Windows 系统 WinRT 库。

## 2026-10-12：1.0.1.0 修复与验证

预览列表高度按实际客户区列数及内容计算，并以屏幕工作区为上限；列表增减、列数变化及窗口恢复后重新计算布局，达到上限后才启用内部滚动。DWM 属性缓存同时跟踪源窗口尺寸；源窗口缩放、WindowCue 失焦及目标前台切换时重新提交预览属性，保持预览目标矩形与卡片布局一致。

从仓库根目录执行 `& .\WindowCue.Native\build.ps1 -BuildRegressionTests`，中文、英文编译均通过；桌面 UI 回归返回 `CN=0`、`EN=0`。验证覆盖：

- 100%、125%、150%、200% 模拟 DPI，列数临界、列表增减、高度封顶、末行卡片命中及 `WM_SIZE` 恢复。
- 实建 DWM 源窗口的同比例缩放及极端宽高变化、失焦后属性强制重新提交、真实全局快捷键前台切换与最小化恢复。

测试使用独立配置目录。上述结果验证布局与 DWM 更新路径；用户截图中真实 ChatGPT 窗口的像素溢出尚未复现，不能据此宣称已完成该应用的像素级验证。中文正式身份 MSIX 的构建及认证边界见 [打包验证记录](../packaging/VALIDATION.md)。

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
- 缩略图属性缓存跟踪目标矩形、可见性及源窗口尺寸；常规刷新避免重复提交，失焦及前台切换时强制重新提交；
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
- 启动后静默进入系统托盘，关闭及最小化也进入托盘，双击托盘恢复，托盘菜单可退出；
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
