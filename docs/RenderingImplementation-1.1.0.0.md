# WindowCue 1.1.0.0 矢量渲染改造记录

> 已被 [1.1.1.0 边框修复](ChromeFix-1.1.1.0.md) 替代。用户报告后已复现：此版本保留的系统标题栏样式与禁用 DWM 的组合会让经典外框重新覆盖界面。以下记录是当时的静态和布局验证，不能作为点击/激活无闪烁的证明。

日期：2026-10-23。本文对应 [整改方案](RenderingRemediation-2026-10-23.md) 的实现，交付状态为源码和本地候选构建。Windows 10 实机、真实混合 DPI 多屏及已安装新版 MSIX 的验证仍待补齐；完成这些验收前不作为稳定 Release 发布。

## 已实施

| 模块 | 实现 |
| --- | --- |
| `WindowCue.Native/src/RenderContext.h` | 每 HWND 的持久 Direct2D 窗口目标；不透明 BGRA、浮点 DIP、几何抗锯齿；DirectWrite 字体/布局缓存；矢量图标路径；设备资源重建和软件后端 |
| `WindowCue.Native/src/Theme.h` | 具名颜色赋值；亮色底 `#F5F8FC`；统一暗色层级；绑定/取消绑定分开前景色；高对比度系统颜色；正常文本对比度自测 |
| `WindowCue.Native/src/WindowChrome.h` | 撤销扩展玻璃；应用标题栏直接绘制；DPI 系统缩放边框命中；最大化工作区；记录可选 DWM 调用结果，不依赖 Windows 11 颜色属性 |
| `WindowCue.Native/src/Automation.h` | 在真实原生控件 HWND 上提供 UI Automation Toggle/Invoke、状态变化通知、名称和焦点语义；没有额外绘制一套系统按钮 |
| `WindowCue.Native/src/Main.cpp` | 主界面和设置的统一直接绘制；自绘标题栏窗口控制、捕获取消和命中；保留原生窗口消息、系统菜单和快捷键路径；连续绘制失败有有界重试 |
| DWM 预览边界 | 维持原有顶级窗口缩略图关系。`BeginPaint` 前读取实际脏区，将复杂区域显式传入 D2D 几何裁剪；不假定 D2D 会继承 HDC 的非矩形裁剪 |

主界面及设置不再使用 GDI RoundRect/DrawText、HBITMAP UI 图标、AlphaBlend 或 BitBlt 绘制。太阳、月亮、齿轮、GitHub 和窗口控制都直接绘制几何。旧 `SvgIconRenderer.cpp` 只供历史像素诊断工具使用，已经移出产品、CMake、界面回归和视觉程序的编译目标；`--svg-self-test` 保留为新矢量几何/主题检查的兼容别名，新名称为 `--render-self-test`。

品牌和第三方应用图标属于内容，通过 WIC 解码原生 HICON；自身大图标使用现有 256px 资源，避免把默认小图标放大。DWM 预览仍是实时窗口关系，没有改为定时截图。

标题栏在主窗口及设置窗口内不透明绘制。设置的内容区仍约 `300×106 DIP`，加上 `34 DIP` 自绘标题栏后完整客户区约 `300×140 DIP`；外部总尺寸与原先原生标题栏加内容的尺寸相近。开关保持 `36×20 DIP`，GitHub 保持 `20 DIP` 图形和 `32×32 DIP` 点击区，没有常驻说明、开启/关闭文字或 GitHub 底色按钮。

布局和 Win32/DWM 命中的边界仍使用原有整数物理像素矩形，进入渲染器后转换为浮点 DIP，避免在同一次渲染中混用单位。尚未把全部业务布局重写为浮点布局引擎。

## 验证方法与结果

当前宿主记录：Windows build `26200.9711`，DisplayVersion `25H2`。这是一套宿主环境，不等于完成 Windows 10/11 两套系统验证。

| 检查 | 实际结果 |
| --- | --- |
| 中英文产品及 UI 回归 | Release/x64 编译成功，最终 `REGRESSION_CN=0`、`REGRESSION_EN=0` |
| 矢量/DWM 原型 | 默认与软件后端均 `result=0`、`complex_clip_preserved=1`；包含资源重建、动态源内容变化和无需逐帧重提的 DWM 关系 |
| 中英文真实窗口视觉 | 最终 `CAPTION_CN=0`、`CAPTION_EN=0`；每语言完整场景连续切换主题 20 次，预览中心像素 20/20 正确；无自启动修改 |
| 真实 DPI | 当前桌面 `120 DPI`；96/144/192 为明确标注的模拟布局，未改系统缩放设置 |
| 原生自测 | 两语言便携与 CN MSIX 的绑定、矢量几何/主题、隔离启动自测均为 `0`；旧 SVG 参数调用实际为新矢量自测 |
| 便携启动 | 两语言静默主窗口、消息响应、默认隔离启动注册、重复启动退出和解压哈希检查通过 |
| MSIX | MakeAppx 完整校验、正式 PFN、默认启用 StartupTask、解包 EXE 哈希通过；未安装新版包 |

最终默认后端视觉证据：`WindowCue.Native/artifacts/caption-visual/20261023-150104-735f2a8f-CN/` 和 `20261023-150110-5c1ba703-EN/`，含主窗口、设置三态、完整卡片/预览/取消绑定场景及报告。原型记录：`WindowCue.Native/artifacts/vector-preview/20261023-150115-d5cad33a/result.txt`。

复现命令：

```powershell
& .\WindowCue.Native\build.ps1 -BuildRegressionTests
& .\WindowCue.Native\tests\Build-VectorPreview.ps1
& .\WindowCue.Native\tests\Build-CaptionVisual.ps1 -Language CN
& .\WindowCue.Native\tests\Build-CaptionVisual.ps1 -Language EN
& .\release\Build-Portable.ps1 -RunSelfTests -RunStartupTest
& .\packaging\Build-Msix.ps1 -Language CN -Mode Store -RunSelfTests
```

UI 回归程序必须使用新建的 `WINDOWCUE_SETTINGS_DIR` 隔离目录运行。视觉工具自动创建隔离目录，不调用产品启动入口、不枚举用户窗口或注册真实启动项。默认和软件后端原型只创建自己的窗口、读取其像素，并在退出时恢复原前台窗口。不要在同一桌面并行运行多个视觉测试。

`WINDOWCUE_RENDER_SOFTWARE=1` 可让测试或诊断运行使用 D2D 软件目标，几何与文字绘制命令保持相同。正常运行优先创建默认目标；默认创建失败后尝试软件目标。软件后端不是图片回退。

Portable artifacts: `dist/release/20261023-150644-d539e295/`

| File | Bytes | SHA256 |
| --- | --- | --- |
| `WindowCue-1.1.0.0-windows-x64-CN.zip` | 350398 | `9CE1C00FC05D2588D0D9283BFEC1B84AACBF76A615ACD92828C91F4A4A064DB0` |
| `WindowCue-1.1.0.0-windows-x64-EN.zip` | 350299 | `05FDA5A7E4F762CA86037226D4D74937BD023B2A1ACD69D3CAD9C92CB87FF29E` |
| `WindowCue-1.1.0.0-windows-x64-CN-Store-Submission-UNSIGNED.msix` | 427385 | `22D903AD9280C6BD32CC6F1FDC10C7921AFB627DAC9ABBBDAEB05BEEE2E076CE` |

MSIX build report: `packaging/output/s1ksec.WindowCue_1.1.0.0_x64_CN_Store.msix.report.json`. The copied MSIX is an unsigned Partner Center submission artifact, not a directly installable package.

依赖检查：新增 DirectWrite 和 UI Automation 的 Windows 系统库。依赖门禁还发现 `api-ms-win-core-debug-l1-1-0.dll`，已用 `dumpbin /imports` 确认其唯一用途是 `OutputDebugStringW`，用于可选 DWM 装饰不可用的调试提示；只增加该明确的系统 API-set 名称，没有放开任意 DLL，也没有复制 System32 运行库。

## 保留的行为与后续验收

现有绑定文件格式、目标窗口识别、全局快捷键、托盘、自启动后端及正式 MSIX 身份保持既有实现。设置的真实原生控件保留 Tab/Enter/Space/Escape 及无障碍 HWND，界面外观由统一渲染器处理。

以下项尚未实测完成，不能由当前构建或截图推断通过：

- Windows 10 1809 (`17763`) 及另一套 Windows 10 环境的真实显示、窗口管理、默认/软件后端和启动行为。
- 100/125/150/175/200% 的所有真实系统缩放组合、混合 DPI 显示器往返，以及真实远程桌面。已有模拟 DPI 截图必须继续标为模拟。
- Windows 11 最大化按钮悬停后的 Snap Layout 实际菜单、触摸操作和屏幕阅读器完整人工流程。自动化已检查相关命中与语义，不代表人工验收完成。
- 实际 GPU 移除/驱动重置；测试中的目标丢弃和重建是模拟资源恢复。
- 已安装新版正式身份 MSIX 的 WinRT 启动开关、注销/登录、WACK 和 Store 认证。未替换用户正在运行的旧版 MSIX，也未修改证书信任。

后续按原整改方案的跨系统矩阵补齐证据，再合入稳定发行流程。
