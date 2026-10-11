# WindowCue MSIX 实际验证记录

验证日期：2026-10-11，Asia/Shanghai。已完成命令行打包准备并生成实际文件；未上传或发布商店，未安装到当前用户，未执行 WACK。

## 最终产物

以下文件位于 `E:\project\vibeCoding\codex侧边聊天\packaging\output\`：

| 文件 | 大小 | 用途 |
| --- | ---: | --- |
| `s1ksec.WindowCue_1.0.0.0_x64_CN_Store.msix` | 349274 字节 | 中文正式身份；Partner Center 上传此未签名包 |
| `s1ksec.WindowCue_1.0.0.0_x64_CN_Store.local-signed.msix` | 352060 字节 | 中文本地测试签名副本 |
| `WindowCue.LocalTest.EN_1.0.0.0_x64_EN_LocalTest.msix` | 349240 字节 | 英文独立测试身份，不可提交商店 |
| `WindowCue.LocalTest.EN_1.0.0.0_x64_EN_LocalTest.local-signed.msix` | 351995 字节 | 英文本地测试签名副本 |

CN Store 原包 SHA256：`0F69E2074178F44B5B90BED5B31D66C12AD2AA01E5DDC82463EC4EA3C3EED929`。

EN LocalTest 原包 SHA256：`4752DED8FEF1C2C50A1EE8746899FC1B1D404B29FB7952F7058C9D2D9EA100E1`。

每个未签名包有 `.report.json`，两个签名副本各有 `.sign-report.json` 和 `.sign.log`。再次从 `C:\Windows` 运行 CN Store 构建成功，生成 `s1ksec.WindowCue_1.0.0.0_x64_CN_Store_20261011-224508-27a3834b.msix`（349276 字节），保留了首批包。重复执行的流程可靠；未承诺包字节级一致。

## 正式中文身份

身份来自用户复制的 Partner Center 产品标识，显示名称及语言用途由用户确认。Chrome 浏览器连接三次返回 `nodeRepl.fetch request failed`，未读取或修改网页。

- Package Identity Name：`s1ksec.WindowCue`
- Publisher：`CN=4A7335E9-E26F-4D4C-9B00-85C55B534682`
- PublisherDisplayName：`s1ksec`
- DisplayName：`WindowCue`
- Package Family Name：`s1ksec.WindowCue_14ska59yrcwza`
- Store ID：`9NGPFSSGL2JX`
- 架构/版本/语言：`x64 / 1.0.0.0 / zh-cn`

MakeAppx 解包计算出的 PFN 与上述 Partner Center PFN 完全相同。英文产品尚未创建或提供身份，`partner-center.json` 的 EN 正式 Name/DisplayName/PFN/StoreId 保持空值，未自行编造。

## 已执行验证

| 检查 | 结果 |
| --- | --- |
| 技术栈 | 当前主要分支为 C++17/Win32/GDI/DWM/Direct2D；另有 .NET 8 WPF 旧分支和根目录 .NET Framework 工具 |
| 工具 | MSVC 14.43.34808、Windows SDK 10.0.22621.0、MSBuild、RC、MakePri、MakeAppx、SignTool、WACK 均存在；本次直接调用 CL/RC，不依赖 GUI 或 MSBuild |
| Release 编译 | CN/EN 均使用 `/O2 /MT /DNDEBUG` 编译成功；PE Machine 为 x64 |
| 版本资源 | 打包专用 EXE 的 FileVersion/ProductVersion 均为 `1.0.0.0`，未改业务资源文件 |
| 运行依赖 | 两个 EXE 均只导入 11 个系统 DLL；无 .NET、VC Redistributable 或第三方 DLL 随包依赖 |
| 图标 | 使用现有 ICO 的 256px 图像生成 8 个透明 PNG，尺寸检查及重复生成哈希检查通过；MakePri 索引成功 |
| 清单 | 正式字段、x64、单一语言、完整信任 EntryPoint、runFullTrust、默认禁用的 startupTask 已配置 |
| MakeAppx | 两个包及 CN 再构建包完整验证成功，未使用 `/nv` |
| 解包 | Identity/Publisher/Version/Architecture 检查通过；打包 EXE 与编译 EXE 的 SHA256 一致 |
| CN 原生自测 | `--binding-self-test` = 0；`--svg-self-test` = 0 |
| EN 原生自测 | `--binding-self-test` = 0；`--svg-self-test` = 0 |
| 测试数据 | 自测通过 `WINDOWCUE_SETTINGS_DIR` 使用各自 artifacts 内独立目录，没有启动普通主窗口或读取/写入真实用户绑定目录 |
| 安全输入检查 | 缺失英文正式标识、CN/EN 相同正式身份、非零 Store 版本第四段均被拒绝 |
| 可重复执行 | 从项目目录及 `C:\Windows` 构建成功；新增运行目录和包文件，不覆盖既有签名/日志/独立版输出 |
| PowerShell | 全部新增脚本在 Windows PowerShell 5.1 语法解析通过 |
| 本地签名 | CN/EN SignTool SHA256 签名成功；ZIP 中存在 AppxSignature.p7x；签名证书 Subject 与各自 Publisher 一致 |
| 本地信任 | 未导入 TrustedPeople/Root；CN `signtool verify /pa /v` 返回 1，具体原因是不受信任的自签名根，记录于 `artifacts/verify-cn-signature.log` |

11 个直接依赖：`d2d1.dll`、`d3d11.dll`、`dwmapi.dll`、`GDI32.dll`、`KERNEL32.dll`、`MSIMG32.dll`、`ole32.dll`、`SHELL32.dll`、`SHLWAPI.dll`、`USER32.dll`、`UxTheme.dll`。

测试签名证书仅存在当前真实 Windows 用户的 `CurrentUser\My` 证书库，私钥不可导出；公开 `.cer` 位于 `packaging/certificates/`：

- CN：`E8FB4C882DD4F3489ADC773B46E67C27E871D291.cer`
- EN：`1D2A1566B881318163CBA63B41CF5DA652033667.cer`

沙箱用户证书操作未完成，已中断该进程；签名随后在真实用户环境完成。没有更改证书信任、系统安全策略或现有凭据。

## MSIX 兼容性检查结论

当前源码支持使用完整信任 MSIX 包运行，未发现必须修改业务代码才能打包的条件。以下是静态代码结论与动态验证边界：

- 快捷键：`Main.cpp:646` 使用 RegisterHotKey，`:1461` 处理 WM_HOTKEY。包内仍是普通桌面进程；已安装包的占用、隐藏后响应和重启注册待测。
- 窗口切换：`Platform.cpp:505` 起调用 ShowWindowAsync/SetForegroundWindow/AttachThreadInput。完整信任不提升权限，系统前台规则及提升权限目标仍有限制。
- 托盘：`Main.cpp:1126` 起使用 Shell_NotifyIconW。关闭/最小化进入托盘；没有 Explorer 重启后的 TaskbarCreated 恢复处理。
- 开机启动：原业务代码没有启动注册逻辑。MSIX 清单增加默认禁用的 startupTask；用户可在 Windows 启动应用设置中启用。源码 `Main.cpp:1652` 始终显示主窗口，没有静默托盘启动参数。注销/登录测试待完成。
- 文件权限：`Platform.cpp:150` 使用 LocalAppData 写用户设置，不写只读安装目录。ICO/SVG 内嵌，不依赖工作目录。
- 配置持久化：`Platform.cpp:538-610` 处理绑定、主题和自身快捷键初始化文件。MSIX AppData 虚拟化、独立版旧配置读取、升级/卸载后的物理文件行为不能仅凭逻辑路径证明，需在测试账户验证。
- 多实例与更新：没有单实例互斥体或专门 Store 更新恢复逻辑，WM_CLOSE 隐藏到托盘；重复启动、更新关闭和重新激活需动态复测。

此次没有运行已安装包、WACK 或 UI/DWM 回归测试程序。原生自测与完整包结构验证不等于已安装 MSIX 兼容性认证。隔离测试账户是为了避免 `LoadBindings()` 筛选失效 HWND 后保存配置改动现有绑定；这不是已证实的 MSIX 缺陷。

## 剩余事项与下一步

1. 在独立 Windows 测试用户中，按 README 从现有签名报告读取 PackagePath/CertificatePath，执行 `Install-Local.ps1 -TrustCertificate`，完成已安装包功能清单，再执行 `Test-Wack.ps1`。当前包尚未安装、WACK 尚未运行，不能标为通过。
2. 若继续英文独立商店发布，在 Partner Center 新建英文产品，取得真实 IdentityName、预留 DisplayName、PFN、StoreId，核对发布者字段后填写 EN 配置，执行 `Build-Msix.ps1 -Language EN -Mode Store -RunSelfTests`。
3. 中文产品新建 submission，上传表中未签名 CN Store 包，补全市场/定价、分类与年龄分级、中文描述、真实截图、支持信息和适用的隐私政策，说明 runFullTrust 的窗口枚举/快捷键/托盘用途，然后提交认证。微软在发布时提供正式签名。

完整命令、精确工具安装步骤及审核说明见 [README.md](README.md)。新增内容全部在 `packaging/`；原 `WindowCue.Native/build.ps1`、CMake、C# 分支、业务源码和 `dist/` 保留。当前目录没有 Git 元数据，无法验证线上 GitHub 流水线，现有本地独立 EXE 发布方式未被替换。
