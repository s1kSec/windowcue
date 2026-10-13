# WindowCue 1.0.4.0

收紧设置窗口，让自启动开关和 GitHub 入口更简洁。

## 更新

- 缩小自启动开关，移除“开启/关闭”文字，以滑块位置与颜色表示状态。
- 移除“WindowCue 启动后静默驻留后台”说明。
- GitHub 入口仅显示图标，去掉文字及底色按钮，点击打开项目仓库。
- 普通设置窗口收紧为 `300×106 DIP`；Windows 禁用或操作异常时按需显示必要提示。
- 默认自启动、静默托盘、便携版 Run 项及 MSIX StartupTask 保持既有行为。

## 下载

Windows 10 1809 及以上 x64，或 Windows 11 x64。便携 ZIP 完整解压后运行 `WindowCue.exe`，按 Alt+P 或双击托盘打开界面；无需 .NET 或另装 VC++ Redistributable。独立 EXE 未签名。

**`WindowCue-1.0.4.0-windows-x64-CN-Store-Submission-UNSIGNED.msix` 仅供开发者提交 Partner Center，未签名，不能直接安装。普通用户下载便携 ZIP。**

中文 MSIX 沿用正式 PFN `s1ksec.WindowCue_14ska59yrcwza`。Microsoft Store 尚未上架或通过认证；英文正式产品身份仍未提供。

## 校验与验证

| 文件 | SHA256 |
| --- | --- |
| `WindowCue-1.0.4.0-windows-x64-CN.zip` | `71FCA73989F216A14A01D8DC246C91F484FB1BDF3010C03DBA7A1F153394416C` |
| `WindowCue-1.0.4.0-windows-x64-EN.zip` | `581C772F31DB2389792017ABC4AB3F91ED0CD16A255570C618A68D641BAE8E85` |
| `WindowCue-1.0.4.0-windows-x64-CN-Store-Submission-UNSIGNED.msix` | `634BF6881EFD6BA6ED85B4EB1D2C48380CF99D0C17EB97B7E37D83505ADC2A9E` |

提供 `SHA256SUMS.txt`。中文、英文 Release/x64 编译，绑定/SVG/自启动自测，静默启动、消息响应及重复启动检查均通过；解压 EXE 哈希一致，运行依赖仅为 Windows 系统 DLL/API-set。两语言 UI/DWM 回归与实际窗口视觉检查均返回 `0`，明暗主题下的开启、关闭及 Windows 禁用状态、GitHub 纯图标外围像素及 Tab/Enter/Escape 操作通过。中文 MSIX 的正式 PFN、默认启用 StartupTask、MakeAppx 完整验证和解包 EXE 哈希通过。

实际设置窗口检查在 `120 DPI` 桌面完成，普通客户区 `375×133px`，Windows 禁用时扩展为 `375×163px`；自启动 API 使用模拟后端，测试记录 `mock_startup_changes=0`。96/144/192 DPI 仅验证主窗口模拟布局，不代表已完成真实混合多屏验证。

已安装新版 MSIX 的启动开关、真实注销/登录、Windows 10 实机及 WACK 尚未验证。

[隐私政策](https://github.com/s1kSec/windowcue/blob/main/PRIVACY.md) · [反馈问题](https://github.com/s1kSec/windowcue/issues)
