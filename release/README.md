# GitHub 独立版构建

要求 Windows、Visual Studio C++ x64 Build Tools 和完整 Windows 10/11 SDK。复用 `packaging/Tools.ps1` 工具检测，以 C++17、`/O2 /MT`、x64 编译当前 `WindowCue.Native` 源码及内嵌图标。版本读取 `packaging/partner-center.json`；此脚本不修改 Store Identity、Publisher 或 MSIX 流程。

在仓库根目录运行：

```powershell
.\release\Build-Portable.ps1 -RunSelfTests -RunStartupTest
```

默认生成中文和英文两份便携 ZIP，可用 `-Language CN` 或 `-Language EN` 单独构建。可指定已安装的 `-SdkVersion`、`-VisualStudioRoot`。

产物位于独立的 `dist/release/<timestamp-guid>/`，不覆盖既有产物：

- `WindowCue-<version>-windows-x64-CN.zip` / `EN.zip`：各含 `WindowCue.exe`、对应语言 `README.txt`、原始 `PRIVACY.md` 及其相对链接 `PRIVACY-AUDIT.md`。
- `SHA256SUMS.txt`：ZIP 的 SHA256，适合随 Release 发布。
- `report.json`：构建和验证记录，留在本地；编译/资源/依赖检查日志及隔离测试设置位于语言子目录。

脚本检查 PE x64、版本资源、系统 DLL 导入表及 ZIP 解压前后 EXE 哈希。`/MT` 静态链接 C/C++ 运行时，ZIP 无需 .NET 或另行安装 VC++ Redistributable。`-RunSelfTests` 使用解压后的 EXE 在隔离配置目录执行绑定与 SVG 自测；`-RunStartupTest` 以隐藏窗口启动，验证正常窗口创建和消息循环响应，然后只终止该新测试进程。正常托盘退出未自动验证，报告明确记录此边界；基础测试不代表所有界面或系统版本已经验证。测试不操作已有 WindowCue 进程或用户配置，启动测试需 Windows 桌面。

Release 仅上传验证通过的便携 ZIP 与 `SHA256SUMS.txt`。独立 EXE 未签名。Store 未签名 MSIX 和 LocalTest 签名 MSIX 均不属于 GitHub 正式用户安装包；不得上传证书、私钥、本地测试配置、构建日志或中间产物。

“便携”表示无需安装；程序默认设置仍保存到 `%LOCALAPPDATA%\WindowCue`。
