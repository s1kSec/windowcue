# WindowCue 1.1.1.0 边框闪现修复

日期：2026-10-23。替代存在经典外框闪现的 1.1.0.0 候选版；仍按 Windows 10 1809 / Windows 11 x64 作为兼容目标，以预发布候选交付。旧版静态截图及主题切换测试通过，没有覆盖系统消息返回至下一次 `WM_PAINT` 之间的状态。

## 复现与原因

本机已复现用户截图中的浅蓝色经典系统标题栏，覆盖应用标题栏与齿轮。旧代码同时保留 `WS_CAPTION`、禁用 DWM 非客户区渲染，只处理 `WM_NCCALCSIZE(TRUE)`。应用绘制和系统默认非客户区路径仍存在竞争。改标题、激活/失焦及标题栏交互后，User32 的经典标题栏能直接盖到客户区上；之后重画应用才把它遮回去。

因此不能通过换一张图、补刷背景、延迟重画或隐藏窗口来解决。1.1.1.0 采用以下修改：

- 创建主窗口时移除 `WS_CAPTION`，不再保留一套需要遮盖的系统标题栏。保留 `WS_THICKFRAME`、最小化/最大化及系统菜单行为样式。
- 保留 DWM 非客户区合成，撤销玻璃扩展；应用直接绘制不透明客户区。Windows 11 可选的 `DWMWA_BORDER_COLOR=DWMWA_COLOR_NONE` 用于抑制合成器额外边线，Windows 10 不依赖此属性成功。
- 主窗口和设置窗口均处理初始 RECT 与后续 NCCALCSIZE_PARAMS 两种客户区计算，窗口矩形完整作为客户区。设置窗口激活也使用文档规定的 `WM_NCACTIVATE` 无重画处理。
- 标题、齿轮、主题图标及窗口按钮仍是 Direct2D/DirectWrite 的即时绘制。未增加窗口截图、位图皮肤、绘制后覆盖系统边框或定时补刷措施。

依据：[Microsoft 窗口样式](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-styles)、[客户区计算](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-nccalcsize)、[激活消息](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-ncactivate)、[DWM 非客户区策略](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmncrenderingpolicy) 和 [可选边框属性](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute)。具体故障原因来自本项目旧代码的运行复现，而非仅凭这些文档推测。

## 验证

宿主：Windows build `26200.9711`，真实桌面 `120 DPI`。测试只操作自有窗口，启动后端使用 mock 或独立注册表/设置目录，没有替换正在运行的 Store 应用或变更其开机启动。

新增 `CaptionVisual.cpp::CheckChromeTransitions`，在同步消息返回后立即采样，禁止采样前调用 `UpdateWindow` 或等待重画。读取标题空白区域及四条边的像素，同时断言完整客户区几何；异常时保留自有窗口 PNG。覆盖标题/图标更新、激活/失焦、非客户区绘制、强制重新计算边框、三个窗口按钮悬停/按下、主窗口与设置窗口反复切换，以及最大化/还原后的状态。最大化和还原通过产品按钮处理函数实际执行，尺寸变化后允许正常重画再采样。

| 检查 | 结果 |
| --- | --- |
| 旧版复现 | 46 次即时采样中 20 次像素异常；另有初始客户区计算异常，整体返回 `104`；保存的画面与用户截图一致 |
| CN 默认渲染 | `CAPTION_CN=0`，52 次即时采样全部通过 |
| EN 默认渲染 | `CAPTION_EN=0`，52 次即时采样全部通过 |
| CN 软件渲染 | `CAPTION_SOFTWARE_CN=0`，52 次即时采样全部通过 |
| 中英文原生 UI 回归 | `REGRESSION_CN=0`、`REGRESSION_EN=0`；使用与产品相同窗口样式 |
| 窗口行为 | 最大化、还原、四角缩放命中、系统菜单句柄及两种客户区计算检查通过 |
| 原有视觉检查 | 各轮包含明暗主题、设置开启/关闭/Windows 禁用状态、模拟 DPI、20 次实时 DWM 预览主题切换；mock 启动变更为 0 |

证据目录：

- 旧版：`WindowCue.Native/artifacts/caption-visual/20261023-152954-9629c85f-CN/`。
- 修复 CN：`WindowCue.Native/artifacts/caption-visual/20261023-153421-c2ea141e-CN/`。
- 修复 EN：`WindowCue.Native/artifacts/caption-visual/20261023-153943-d431f60f-EN/`。
- 软件渲染：`WindowCue.Native/artifacts/chrome-checks/20261023-154136-dc383eec/software-CN/`。

重新运行：

```powershell
& .\WindowCue.Native\build.ps1 -BuildRegressionTests
& .\WindowCue.Native\tests\Build-CaptionVisual.ps1 -Language CN
& .\WindowCue.Native\tests\Build-CaptionVisual.ps1 -Language EN
$env:WINDOWCUE_RENDER_SOFTWARE = '1'
& .\WindowCue.Native\tests\Build-CaptionVisual.ps1 -Language CN
Remove-Item Env:\WINDOWCUE_RENDER_SOFTWARE
& .\release\Build-Portable.ps1 -RunSelfTests -RunStartupTest
& .\packaging\Build-Msix.ps1 -Language CN -Mode Store -RunSelfTests
```

同一桌面的 UI 测试顺序运行，UI 回归 EXE 必须传入新建的 `WINDOWCUE_SETTINGS_DIR`。原型与 UI 回归的其他测试方法见 [矢量改造记录](RenderingImplementation-1.1.0.0.md)。

## 发行边界

本次修复使用相同原生源码构建中英文便携版和中文正式身份的 Store MSIX。MSIX 未签名，仅供开发者提交 Partner Center，不能直接安装；构建、自测和 MakeAppx 校验不代表 WACK、商店认证或已安装包运行验证。英文独立 Store 身份仍未配置，不借用中文身份创建英文商店包。

当前测试确认本机原缺陷已经复现并修复。仍待 Windows 10 实机、其他驱动/远程桌面、真实混合 DPI 多屏、Snap Layout 菜单人工流程、已安装新版 MSIX 与注销/登录启动测试；不能将有限像素采样解释为所有设备和所有帧的无闪烁证明。GitHub 使用 `v1.1.1.0-rc.1` 预发布标识，不替换稳定版。


## 构建产物

便携目录：`dist/release/20261023-154217-2d2fe936/`。中英文原生自测、隔离静默启动、默认隔离启动注册、重复启动退出及解压哈希检查通过；MSIX 完整 MakeAppx 校验、StartupTask 清单及解包 EXE 哈希通过。两个隐私政策副本及审计副本保持一致。

| 文件 | 字节数 | SHA256 |
| --- | --- | --- |
| `WindowCue-1.1.1.0-windows-x64-CN.zip` | 350402 | `83128344C8FF9FCFF7639E2C89E256DE050992A0C2497386B9246C6A83789208` |
| `WindowCue-1.1.1.0-windows-x64-EN.zip` | 350272 | `1FF8EECC536FDD7D1DCA00BB2CCC8DBE979847A32D0D67E0C7B6589DEB10A515` |
| `WindowCue-1.1.1.0-windows-x64-CN-Store-Submission-UNSIGNED.msix` | 427407 | `FE235A7A5DD9A9ECB5FDEE7DB0C8F8795D89A7542049D7AE7FB52897D06E5318` |

MSIX 报告：`packaging/output/s1ksec.WindowCue_1.1.1.0_x64_CN_Store.msix.report.json`。GitHub 预发布标签为 `v1.1.1.0-rc.1`，包内版本为符合 Store 规则的 `1.1.1.0`。
