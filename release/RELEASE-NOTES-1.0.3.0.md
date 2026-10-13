# WindowCue 1.0.3.0

修正设置齿轮在小尺寸下被裁切、高 DPI 下未放大的问题，并完善原生标题栏的视觉与交互。

## 更新

- 重画原创六齿圆角齿轮，保留清楚的中心圆孔、均衡描边与点击区域。
- 修正 Direct2D SVG 根节点缩放，全部图标按实际尺寸渲染；同步调整月亮光学居中与 GitHub 留白。
- 标题栏按钮增加 125ms 缓出悬停、1 DIP 按压反馈和中英文原生提示。透明度正确预乘，避免玻璃标题栏出现黑块；遵循 Windows 动效设置，空闲时停止动画计时器。
- 主界面字体依据窗口 DPI 缩放，保持与图标、间距和设置窗口一致。
- 默认自启动、静默托盘、设置开关与 Windows 禁用状态保留行为延续 1.0.2.0。

## 下载

Windows 10 1809 及以上 x64，或 Windows 11 x64。便携 ZIP 完整解压后运行 `WindowCue.exe`，按 Alt+P 或双击托盘打开界面；无需 .NET 或另装 VC++ Redistributable。独立 EXE 未签名。

**`WindowCue-1.0.3.0-windows-x64-CN-Store-Submission-UNSIGNED.msix` 仅供开发者提交 Partner Center，未签名，不能直接安装。普通用户下载便携 ZIP。**

中文 MSIX 沿用正式 PFN `s1ksec.WindowCue_14ska59yrcwza`。Microsoft Store 尚未上架或通过认证；英文正式产品身份仍未提供。

## 校验与验证

| 文件 | SHA256 |
| --- | --- |
| `WindowCue-1.0.3.0-windows-x64-CN.zip` | `AE3318F19E02D9C7F6E543F9A1019F867DE4F39BCA41F7F89E61BA92CBF1564E` |
| `WindowCue-1.0.3.0-windows-x64-EN.zip` | `A697321B6ECE5F4AE9827E655AF229341A1DB7E862874FF9D0C4443DB70B27A2` |
| `WindowCue-1.0.3.0-windows-x64-CN-Store-Submission-UNSIGNED.msix` | `09E47ACCC8E19A0A8514F42C605771FCE3FEBF18E8D7FD37DCC10C081C6C6B03` |

提供 `SHA256SUMS.txt`。中文、英文 Release/x64 编译、绑定/SVG/自启动自测与静默单实例启动检查均通过。UI/DWM 回归返回 CN=0、EN=0。严格原生像素检查为 ICON_VISUAL_ERRORS=0，六个资源覆盖 18/23/27/36px，齿轮无裁切、中心孔透明、重心误差小于 0.005px。中英文专用窗口真实 DWM 检查均为 CAPTION_VISUAL=0，包含悬停中间帧、按压、设置打开、自然工具提示及模拟 DPI。中文 MSIX 的正式 PFN、默认启动清单、MakeAppx 完整验证及解包 EXE 哈希均通过；运行依赖仅为 Windows 系统 DLL/API-set。

视觉检查基于真实 Direct2D 像素、当前 Windows 桌面的专用测试窗口和模拟 DPI。已安装新版 MSIX 的启动开关、真实注销/登录、Windows 10 实机及 WACK 尚未验证。未修改用户实际启动项、既有 WindowCue 进程或证书信任。

[隐私政策](https://github.com/s1kSec/windowcue/blob/main/PRIVACY.md) · [反馈问题](https://github.com/s1kSec/windowcue/issues)
