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

脚本检查 PE x64、版本资源、系统 DLL 导入表及 ZIP 解压前后 EXE 哈希。`/MT` 静态链接 C/C++ 运行时，ZIP 无需 .NET 或另行安装 VC++ Redistributable。`-RunSelfTests` 使用解压后的 EXE 在隔离配置目录执行绑定、SVG 与自启动自测。`-RunStartupTest` 不覆盖 GUI 的 `ShowWindow` 行为，检查应用自行保持主窗口隐藏、消息循环响应、首次默认注册隔离启动值及重复启动退出；随后只终止新建测试进程并清理本次精确隔离注册表子树。测试环境变量将启动注册隔离到 `HKCU\Software\WindowCue\StartupTests\<目录哈希>`，不操作真实 Run/StartupApproved 或已有 WindowCue 进程、用户配置。

正常托盘菜单退出、真实注销/登录自启动及全部 Windows 版本尚需实机验证；报告保留这些边界。启动测试需要 Windows 桌面。`WINDOWCUE_SETTINGS_DIR` 是测试隔离入口，设置该变量时不会注册真实登录启动项。

Release 上传验证通过的 CN/EN 便携 ZIP 与 `SHA256SUMS.txt`。独立 EXE 未签名。可同时附上中文正式身份的未签名 Store MSIX，文件名及说明必须标注 **仅供开发者提交 Partner Center，不能直接安装**，并把哈希纳入校验文件；该附件不表示通过 Store 认证。英文正式产品身份尚未提供。LocalTest MSIX、证书、私钥、本地测试配置、构建日志及中间产物不随 Release 发布。

“便携”表示无需安装；程序默认设置仍保存到 `%LOCALAPPDATA%\WindowCue`。首次正常运行默认启用当前用户登录自启动，并静默驻留托盘；在主题按钮左侧的齿轮设置中关闭或开启。`Alt+P` 或双击托盘图标打开界面；程序未运行时 `--show` 启动并显示界面，重复启动静默退出。启用期间保留 EXE 路径，移动后从新位置运行会更新仍启用的注册；删除前关闭自启动。
