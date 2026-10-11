# WindowCue Microsoft Store MSIX 打包

此目录为 `WindowCue.Native` 增加独立的命令行 MSIX 构建、检查与本地测试流程。中文和英文分别生成一个只含对应语言 EXE 的包，不合并为双语言程序或双入口包。现有业务源码、`WindowCue.Native/build.ps1`、旧 .NET 分支及 `dist/` 独立 EXE 发布方式保持不变。

## 当前成果与证据边界

2026-10-11 已在当前 Windows 环境实际编译、打包并解包检查：

| 产物 | 用途 | 大小 | 当前结果 |
| --- | --- | ---: | --- |
| `output/s1ksec.WindowCue_1.0.0.0_x64_CN_Store.msix` | 中文 Store 正式身份包，未签名 | 349274 字节 | Release/x64；MakeAppx 完整验证；PFN 与 Partner Center 一致 |
| `output/WindowCue.LocalTest.EN_1.0.0.0_x64_EN_LocalTest.msix` | 英文本地测试身份包，未签名 | 349240 字节 | Release/x64；MakeAppx 完整验证；不可提交商店 |

两次构建的 `--binding-self-test` 和 `--svg-self-test` 均返回 `0`；解包 EXE 的 SHA256 与编译产物一致；导入 DLL 检查只发现 Windows 系统 DLL，没有需随包分发的 .NET、VC Redistributable 或其他第三方运行库。

每个包都有同名 `.report.json`，记录身份、实际工具路径、依赖、测试、大小、哈希和日志目录。`artifacts/` 保存每次独立的编译、资源索引、打包、解包日志与工作目录。重建遇到同名包时生成带运行标识的新文件，不覆盖已有包与签名。

本节是上述首批构建的记录。**原生 EXE 自测不等于已安装 MSIX 的功能测试，MakeAppx 验证不等于 WACK 或 Store 认证。** 安装后的窗口切换、快捷键、托盘、配置物理路径、更新持久化及开机启动仍需在隔离 Windows 用户中测试；尚未执行商店上传或提交认证。

## 实际技术栈及原有发布方式

| 分支 | 实现 | 原有构建 | 运行依赖 |
| --- | --- | --- | --- |
| `WindowCue.Native`，本目录打包目标 | C++17、Win32、GDI、DWM 缩略图、Direct2D/Direct3D | PowerShell 直接调用 MSVC `cl.exe` 与 SDK `rc.exe`；另有 CMake 配置 | `/MT` 静态 C/C++ 运行时，Windows 系统 DLL |
| `WindowCue`，旧分支 | C#、.NET 8、WPF、Windows Forms、Win32 P/Invoke | `.dotnet-sdk/dotnet.exe publish -c Release -r win-x64` | 原脚本 `--self-contained false`，需 .NET 8 Desktop Runtime |
| 根目录聊天快捷键工具 | C#、.NET Framework、Windows Forms/UI Automation | 根目录 `build.ps1` 调用 Windows 的 `csc.exe` | Windows .NET Framework |

当前 MSIX 流程明确选择原生分支，不在工具缺失时自动切换到旧 .NET 分支。Release 使用 `/O2 /std:c++17 /MT` 和 x64 工具链，直接编译源码，不需要 Visual Studio 图形界面、MSBuild 项目或 NuGet 恢复。

代码证据：`WindowCue.Native/build.ps1:83`、`WindowCue.Native/CMakeLists.txt:4`、`WindowCue/WindowCue.csproj:4`、`WindowCue/build.ps1:24`。原生资源脚本已有图标、SVG 与兼容清单，未包含 `VERSIONINFO`；打包脚本在本次 `artifacts/obj` 中生成独立 `Version.rc`，将包版本写入打包专用 EXE 的文件/产品版本资源，不编辑原业务源码。

继续原有独立 EXE 构建：

```powershell
Set-Location 'E:\project\vibeCoding\codex侧边聊天'
& .\WindowCue.Native\build.ps1
```

原输出仍为 `dist\WindowCue.Native\WindowCue_Native_CN.exe` 与 `WindowCue_Native_EN.exe`。GitHub 源码仓库为 [s1kSec/windowcue](https://github.com/s1kSec/windowcue)。独立便携 ZIP 使用 `release/Build-Portable.ps1` 构建，下载见 [GitHub Releases](https://github.com/s1kSec/windowcue/releases)；隐私政策的公开页面为 [PRIVACY.md](https://github.com/s1kSec/windowcue/blob/main/PRIVACY.md)。MSIX 打包不会替换上述脚本或发布文件；Store 未签名包和本地测试签名包不作为 GitHub 用户安装包发布。

## 已发现的构建与验证工具

当前 MSVC 工具集目录为 `14.43.34808`，SDK 目录为 `10.0.22621.0`；宿主 Windows 的系统版本接口返回 `10.0.26200.0`。目录版本与实际文件版本可能不同，以下为实际检查结果。

| 工具 | 实际版本 | 当前路径 |
| --- | --- | --- |
| MSVC `cl.exe` | 文件版本 `19.43.34809.0` | `E:\tools\VS_C++_Tool\VC\Tools\MSVC\14.43.34808\bin\Hostx64\x64\cl.exe` |
| `link.exe`、`dumpbin.exe` | `14.43.34809.0` | 同一 `Hostx64\x64` 目录 |
| MSBuild | `17.13.19.12613` | `E:\tools\VS_C++_Tool\MSBuild\Current\Bin\MSBuild.exe` |
| `rc.exe`、`makeappx.exe`、`makepri.exe` | 产品版本 `10.0.22621.3233` | `C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\` |
| `signtool.exe` | 产品版本 `10.0.22621.3233` | 上述 SDK x64 目录 |
| WACK `appcert.exe` | `10.0.22621.3233` | `C:\Program Files (x86)\Windows Kits\10\App Certification Kit\appcert.exe` |
| 系统 .NET CLI | 已发现，本原生打包流程不使用 | `C:\Program Files\dotnet\dotnet.exe` |

查看当前机器实时检测结果：

```powershell
& .\packaging\Build-Msix.ps1 -CheckToolsOnly
```

`Tools.ps1` 通过 `vswhere`、Windows Kits 注册表与实际目录寻找完整工具链，验证编译器、静态库、SDK 头文件/库和打包工具存在。也可显式指定已安装版本：

```powershell
& .\packaging\Build-Msix.ps1 -CheckToolsOnly `
    -VisualStudioRoot 'E:\tools\VS_C++_Tool' -SdkVersion '10.0.22621.0'
```

本次机器已具备编译与打包依赖，无需为完成这次构建安装软件。微软当前 SDK 文档将 22621 列为停止支持；正式发布前宜在受支持 SDK 上重新验证，工具发现逻辑支持并行安装的新 SDK。SDK 安装版本不决定包的最低系统版本。[Windows SDK 支持状态](https://learn.microsoft.com/en-us/windows/apps/windows-sdk/)

### 缺少依赖时的精确安装步骤

在管理员 PowerShell 中，可安装 Visual Studio 2022 Build Tools 的 C++ x64 编译器、MSBuild 与受支持的 SDK；无需安装完整 Visual Studio IDE。

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools --exact `
    --override '--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.26100'
```

没有 `winget` 时，下载官方 bootstrapper 并执行相同组件安装：

```powershell
$installerPath = Join-Path $env:TEMP 'WindowCue-vs-buildtools.exe'
Invoke-WebRequest -Uri 'https://aka.ms/vs/17/release/vs_buildtools.exe' -OutFile $installerPath
& $installerPath --quiet --wait --norestart `
    --add Microsoft.VisualStudio.Workload.VCTools `
    --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    --add Microsoft.VisualStudio.Component.Windows11SDK.26100
```

返回 `0` 是成功，`3010` 是成功但需要重启。若已有 Build Tools 而缺少组件，用 Visual Studio Installer 的命令行 `modify --installPath <实际安装目录> --add <上述组件ID> --quiet --norestart` 增补；`--wait` 只属于 bootstrapper，不能传给 Installer 的 `setup.exe`。完成后重新执行 `-CheckToolsOnly`。

若只有 SDK 打包工具缺失，可独立安装 SDK：

```powershell
winget install --id Microsoft.WindowsSDK.10.0.26100 --exact
```

若缺少 WACK，从[官方 SDK 下载页](https://learn.microsoft.com/en-us/windows/apps/windows-sdk/)启动 SDK 安装器，选择 **Windows App Certification Kit**；同时保留 Windows 桌面 C++ 头文件/库、应用打包及签名工具。SDK 的小型组件选择界面不需要 Visual Studio IDE。完成后检查 `appcert.exe` 和 `-CheckToolsOnly`，不把下载完成当作组件安装成功。

安装参考：[Build Tools 组件 ID](https://learn.microsoft.com/en-us/visualstudio/install/workload-component-id-vs-build-tools?view=vs-2022)、[安装器命令行参数](https://learn.microsoft.com/en-us/visualstudio/install/use-command-line-parameters-to-install-visual-studio?view=vs-2022)、[官方 bootstrapper 示例](https://learn.microsoft.com/en-us/visualstudio/install/build-tools-container-issues?view=vs-2022)。

## 正式身份与中文/英文独立产品

`partner-center.json` 保存打包输入，正式模式要求身份完整，禁止以占位符生成可提交的正式包。当前中文产品已由用户确认：

| 字段 | 中文 CN 产品 |
| --- | --- |
| `IdentityName` | `s1ksec.WindowCue` |
| `Publisher` | `CN=4A7335E9-E26F-4D4C-9B00-85C55B534682` |
| `PublisherDisplayName` | `s1ksec` |
| `DisplayName` | `WindowCue` |
| `ExpectedPackageFamilyName` | `s1ksec.WindowCue_14ska59yrcwza` |
| `StoreId` | `9NGPFSSGL2JX` |
| 包版本 | `1.0.0.0` |

英文独立产品尚未提供 `IdentityName`、预留的 `DisplayName`、`ExpectedPackageFamilyName` 和 `StoreId`。目前 EN 的 Publisher/PublisherDisplayName 使用相同开发者账户值；创建 EN 产品后仍需核对其 Product identity 页面。正式商店构建至少需要实际 Name、Publisher、PublisherDisplayName 和预留名称；填写 PFN 可额外验证身份计算结果。不得在中文 Name 后自行追加 `.EN` 作为正式身份。

获取路径：Partner Center → Apps and games → 对应产品 → **Product management → Product identity**。Name、Publisher、PublisherDisplayName 应原样复制，保留大小写、空格和标点。PFN、Store ID 是额外校验/提交参考，不写进包的 `Identity` 属性。[官方身份字段说明](https://learn.microsoft.com/en-us/windows/apps/publish/view-app-identity-details)

用户要求中文、英文独立发布，因此应各自预留并创建一个 Store 产品。两个主包不能仅用不同语言或不同 `.msix` 文件名区分同一产品身份：相同产品中相同版本与架构不能同时重复；不同版本又参与更新排序。相同 PFN 在 Windows 中属于更新关系，不能当作两个独立安装的应用。脚本检测 CN/EN 正式身份碰撞并拒绝打包。[商店版本与架构规则](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msix/app-package-requirements)、[安装与更新关系](https://learn.microsoft.com/en-us/powershell/module/appx/add-appxpackage?view=windowsserver2025-ps)

`LocalTest` 模式使用明确标注的测试身份 `WindowCue.LocalTest.CN` 或 `WindowCue.LocalTest.EN`、`CN=WindowCue Local Test`。这些身份仅用于本地实验，禁止提交 Partner Center，也不能证明 EN 正式商店身份有效。

## 编译与打包命令

推荐在 x64 Windows PowerShell 5.1 或更新的 PowerShell 中，从项目根目录执行：

```powershell
Set-Location 'E:\project\vibeCoding\codex侧边聊天'

# 中文正式 Store 身份包；运行两个隔离配置目录中的原生自测。
$cn = & .\packaging\Build-Msix.ps1 -Language CN -Mode Store -RunSelfTests

# 英文独立本地测试包；EN 正式身份尚未提供。
$en = & .\packaging\Build-Msix.ps1 -Language EN -Mode LocalTest -RunSelfTests

$cn.PackagePath
$en.PackagePath
```

英文正式产品建立并填入 `partner-center.json` 后，单独运行：

```powershell
& .\packaging\Build-Msix.ps1 -Language EN -Mode Store -RunSelfTests
```

可用 `-IdentityConfig '其他配置.json'`、`-Version '1.0.1.0'`、`-SdkVersion`、`-VisualStudioRoot` 控制输入。版本是 `Major.Minor.Build.0`，第一段至少 `1`，各段不超过 `65535`，第四段为 Store 保留，必须 `0`；提交更新时提高前三段之一。同一 Windows 用户重装不同内容但相同身份/版本的包会产生冲突，更新测试也应递增版本。

构建依次编译 Release EXE、生成文件版本资源、检查 x64 PE 与导入 DLL、复制当前应用实际运行文件、生成 PNG 资产及 `resources.pri`、填充清单、运行 MakeAppx 完整验证、解包核对身份/PFN/EXE 哈希，并写报告。不会打包已有用户配置、编译工具、自测日志或旧 .NET 运行库。

本项目 `/MT` 下没有外部运行库需要收集。若未来业务引入非系统 DLL，构建会拒绝继续并列出依赖，需要先审查和补充明确依赖，不静默忽略 DLL 或从机器任意目录复制。

## 清单、图标及版本配置

`AppxManifest.xml` 是输入模板，不能直接安装；正式清单位于每次 `artifacts/<运行标识>/stage/AppxManifest.xml`。

- `ProcessorArchitecture="x64"`，`Windows.Desktop` 最低版本 `10.0.17763.0`。
- `EntryPoint="Windows.FullTrustApplication"` 和 `runFullTrust`，以当前登录用户的普通桌面进程权限运行；完整信任不授予管理员权限或 `uiAccess`。
- CN 仅声明 `zh-cn`，EN 仅声明 `en-us`，各包只有 `app\WindowCue.exe` 一个语言版本。
- 图标从已有 `WindowCue/Assets/WindowCue.ico` 生成；含 50×50 StoreLogo、44×44/150×150 主图及 16/24/32/48/256 目标尺寸 PNG，使用 MakePri 索引。
- 声明 `windows.startupTask`，`Enabled="false"`，由用户在 Windows 启动应用设置中主动启用。

需要重建图标时运行 `& .\packaging\New-Assets.ps1`；正常构建也会生成图标。商店详情页所需截图和营销图应另外准备，不能用安装资产替代实际应用截图。[手工桌面打包清单](https://learn.microsoft.com/en-us/windows/msix/desktop/desktop-to-uwp-manual-conversion)、[MakeAppx](https://learn.microsoft.com/en-us/windows/msix/package/create-app-package-with-makeappx-tool)

## 本地签名和安装

Store 输出保留未签名状态，供微软商店认证后重新签名。无需购买 CA 代码签名证书来提交 MSIX；GitHub/站外分发和本地安装的签名要求不同。[Store MSIX 签名要求](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msix/app-package-requirements)

### 只生成测试签名，不自动信任或安装

```powershell
$signed = & .\packaging\Sign-Local.ps1 `
    -PackagePath '.\packaging\output\s1ksec.WindowCue_1.0.0.0_x64_CN_Store.msix' `
    -CreateCertificate

$signed | Format-List PackagePath, CertificatePath, CertificateThumbprint, SignatureStatus
```

也可传入 `-CertificateThumbprint '<已有当前用户证书指纹>'`。证书 Subject 必须与包 Publisher 完全一致，具有私钥和代码签名 EKU。脚本只在 `Cert:\CurrentUser\My` 创建/复用专属测试证书；生成的私钥不可导出，不生成 PFX。导出至 `packaging/certificates/` 的 `.cer` 仅包含公钥。签名使用 SHA256，把包复制为 `.local-signed.msix`，保留原未签名包并输出 `.sign.log`、`.sign-report.json`。

**Sign-Local 不添加信任、不安装应用。** 未信任自签名证书时，`Get-AuthenticodeSignature` 可能报告 `NotTrusted` 或 `UnknownError`，应同时查看具体的 `SignatureStatusMessage`。本次两个签名副本实际均提示证书链终止于不受信任的根证书；SignTool 签名成功，CN 的 `verify /pa /v` 返回 `1`，原因正是尚未信任测试证书。签名成功与系统信任是两项独立结果。完整信任与签名验证在安装步骤的 `signtool verify /pa` 中检查。[微软签名指南](https://learn.microsoft.com/en-us/windows/msix/package/sign-msix-package-guide)、[SignTool](https://learn.microsoft.com/en-us/windows/msix/package/sign-app-package-using-signtool)

已生成 CN/EN 的 `.local-signed.msix` 和配套 `.sign-report.json`，可直接从现有报告读取路径进行安装，不必重新签名。重复对同一未签名文件签名时，脚本会保护已经存在的签名副本并报错；需新建时先重新构建，使用新的包路径。

### 在隔离 Windows 测试用户中安装

建议先切换到单独的 Windows 测试账户，再签名、信任、安装和首次启动。WindowCue 会加载并保存当前用户配置；同一真实用户的测试启动可能更新原快捷键绑定，CN/EN/便携版也可能争抢全局 `Alt+P`。本次原生自测通过进程级 `WINDOWCUE_SETTINGS_DIR` 指向构建工作目录；此做法不证明 Start 菜单启动也具有该环境变量，因此隔离 Windows 用户更稳妥。

在上述测试用户的管理员 PowerShell 中执行；若重开终端，先从 `.sign-report.json` 读回路径：

```powershell
$signed = Get-Content -LiteralPath `
    '.\packaging\output\s1ksec.WindowCue_1.0.0.0_x64_CN_Store.local-signed.msix.sign-report.json' `
    -Raw -Encoding UTF8 | ConvertFrom-Json

& .\packaging\Install-Local.ps1 -PackagePath $signed.PackagePath `
    -CertificatePath $signed.CertificatePath -TrustCertificate
```

`-TrustCertificate` 明确将匹配的测试证书公钥导入 **LocalMachine\TrustedPeople**，需要管理员权限，会影响该机器的证书信任。证书已信任时可省略该开关：

```powershell
& .\packaging\Install-Local.ps1 -PackagePath $signed.PackagePath
```

脚本先用 SignTool 验证，再调用普通 `Add-AppxPackage`，不使用强制终止、强制更新或降级参数，也不自动打开应用。Add-AppxPackage 只给执行命令的 Windows 用户安装；如果使用其他管理员账户提升权限，包也会安装给该管理员，请保持测试账户一致。

安装后从 Start 菜单激活，或执行 CN 正式身份入口：

```powershell
explorer.exe 'shell:AppsFolder\s1ksec.WindowCue_14ska59yrcwza!WindowCue'
Get-AppxPackage -Name 's1ksec.WindowCue' |
    Select-Object Name, Version, Architecture, PackageFamilyName, InstallLocation
```

其他身份用 Install-Local 输出的实际 PFN 替换。不要直接运行 `artifacts/stage/app/WindowCue.exe` 来代替已安装包测试，也不要修改 `WindowsApps` 目录权限。

上述辅助脚本使用完整开发工具链检测，适合当前构建机器。仅接收测试包的机器不需要 C++ 编译器，可在隔离测试账户的管理员 PowerShell 中使用 Windows 自带命令：

```powershell
# 先核对公开测试证书指纹和包签名证书指纹相同。
$packagePath = '<收到的已签名 MSIX 绝对路径>'
$certificatePath = '<对应公开 CER 绝对路径>'
$certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($certificatePath)
$signature = Get-AuthenticodeSignature -LiteralPath $packagePath
if (-not $signature.SignerCertificate -or $signature.Status -eq 'HashMismatch' -or
    $certificate.Thumbprint -ne $signature.SignerCertificate.Thumbprint) { throw '证书或签名不匹配' }
Import-Certificate -FilePath $certificatePath -CertStoreLocation 'Cert:\LocalMachine\TrustedPeople'
Add-AppxPackage -Path $packagePath
```

这会信任对应测试签名证书；`Add-AppxPackage` 在安装时执行包签名及完整性验证。无需开启 Developer Mode 或关闭证书验证。在 Windows 10 1809 等旧测试系统上如遇企业侧载策略限制，应按本机策略处理；不自动修改系统策略。

## MSIX 兼容性检查与复测清单

静态分析确认目前没有必须修改业务源码才能开始打包的问题。下表中的行为预期基于当前代码和完整信任桌面进程模型，实际安装结果仍须复测。

| 项目 | 当前实现/证据 | 安装后需要确认 |
| --- | --- | --- |
| 全局快捷键 | `Main.cpp:472,602,646` 使用 `RegisterHotKey`；默认自身快捷键 `Alt+P` | 在普通目标窗口中绑定/唤出/隐藏；已有占用时保留配置；重启后重新注册；不让 CN/EN 同时争抢 |
| 枚举与窗口切换 | `Platform.cpp` 枚举顶层窗口；`:512` 使用 `SetForegroundWindow`，`:519` 使用 `AttachThreadInput` | 普通、最小化、Win+D 后窗口；同名多个窗口准确绑定；前台限制下行为；中等权限不能保证控制提升权限窗口 |
| DWM 实时预览 | `Main.cpp:688` 使用 `DwmRegisterThumbnail` | 窗口切换、悬停、缩放/DPI、Win+D 和多窗口；包激活下预览持续更新 |
| 系统托盘 | `Main.cpp:1135` 使用 `Shell_NotifyIconW`；关闭/最小化进入托盘，双击恢复 | 关闭后进程驻留，托盘恢复与退出；Explorer 重启后的实际表现也须验证 |
| 开机启动 | 清单 `desktop:StartupTask` 默认禁用；源码未实现启动任务 API 或注册表 Run 写入 | 首次启动后在设置 → 应用 → 启动中主动启用；注销/登录验证；禁用后不再启动 |
| 文件权限 | `Platform.cpp:150` 用 `SHGetKnownFolderPath(FOLDERID_LocalAppData)` 解析配置目录，不写 EXE 目录 | 从只读 WindowsApps 安装路径运行仍能保存配置；普通用户不需管理员权限 |
| 配置持久化 | `Platform.cpp:547` 保存 `bindings.json`，`:591` 保存 `appearance.txt`，`:607` 保存初始化标记 | 首次运行、退出重启、版本升级、卸载后重装；检查逻辑路径及实际物理路径，确认是否采用 MSIX 虚拟化/私有映射 |
| 原配置与语言版共存 | `Main.cpp:631` 的 LoadBindings 会匹配并重新保存存活 HWND；旧版共用 `%LocalAppData%\WindowCue` | 不在真实用户首次启动测试；分别确认两个产品的配置隔离/共享实际行为，不承诺自动迁移或共享 |

完整信任意味着应用仍以普通用户中等完整性等级运行，MSIX 安装目录只读，部分文件/注册表操作可能被虚拟化。不能仅从源代码中的 `%LocalAppData%\WindowCue` 推断安装后所有 Windows 版本的物理存储位置与卸载行为。[打包桌面应用运行模型](https://learn.microsoft.com/en-us/windows/msix/desktop/desktop-to-uwp-behind-the-scenes)

当前 `startupTask` 不新增设置按钮；用户在 Windows 中管理启用状态。不要把路径固定到版本化 WindowsApps 的传统启动快捷方式作为 Store 开机启动方案，也不要使用 `shell:startup` 搬运包内 EXE。[StartupTask 官方说明](https://learn.microsoft.com/en-us/uwp/api/windows.applicationmodel.startuptask?view=winrt-26100)

原有代码还没有处理 Explorer 重启后的 `TaskbarCreated` 托盘重建，没有单实例互斥体，启动时始终显示主窗口，也没有专门的 Store 更新关闭/重启处理。它们是应复测的现有行为：启用开机启动后会显示主窗口；重复启动可能出现快捷键占用；Explorer 重启后可能需要重启 WindowCue 恢复托盘。这次仅增加打包目录，未扩展业务功能。

## WACK 验证

完成隔离测试用户中的证书信任后，以该用户的管理员 PowerShell 执行：

```powershell
& .\packaging\Test-Wack.ps1 -PackagePath $signed.PackagePath
```

WACK 会按流程安装、激活并可能关闭应用，必须在活动用户会话中运行。脚本写入独立 `artifacts/wack-<运行标识>/report.xml` 和日志；仍需逐项阅读报告，不能只看进程返回码或文件存在就宣称认证通过。

等价的手工命令：

```powershell
$appcert = 'C:\Program Files (x86)\Windows Kits\10\App Certification Kit\appcert.exe'
& $appcert reset
& $appcert test -appxpackagepath $signed.PackagePath `
    -reportoutputpath 'E:\project\vibeCoding\codex侧边聊天\packaging\artifacts\wack-manual-report.xml'
```

手工重复运行时请为报告使用新的路径，以保留旧结果。生产发布宜使用受支持 SDK 中的最新 WACK 复测。[WACK 官方命令行文档](https://learn.microsoft.com/en-us/windows/uwp/debug-test-perf/windows-app-certification-kit)

## Partner Center 提交步骤与剩余事项

1. 中文产品确认 Product identity 与 `partner-center.json` 一致；英文需另建产品，取得全部真实字段后再构建 EN Store 包。
2. 在隔离 Windows 用户中完成安装兼容性清单，运行 WACK 并检查所有失败/警告，保留报告与实际测试系统信息。
3. 进入 CN 产品，新建 submission；设置市场、受众、定价、分类、年龄分级和产品声明。
4. Packages 上传 **未签名的 CN Store `.msix`**；核对其 x64、版本、支持语言、包名和 PFN。不要上传 EN LocalTest 包、`.cer` 或本地签名包。以后 EN 在自己的产品中提交 EN Store 包。
5. 准备准确的中文商店说明、至少一张真实应用截图、Logo、功能说明与支持信息；英文产品准备英文材料。按实际个人信息处理填写隐私政策 URL，需提供时使用公开可访问的网址，不编造未上线地址。
6. Submission options 填写 `runFullTrust` 使用原因及测试操作说明。提示审核员关闭/最小化会驻留托盘，可双击托盘恢复、右键退出；`Alt+P` 可唤出；启动功能默认禁用且由用户控制。
7. 所有必填项和包校验通过后提交认证。最终商店签名由 Microsoft 提供，本目录不会替用户上传或触发正式发布。

可使用的完整信任说明：

> WindowCue 是本地桌面窗口管理工具。它需要在用户的普通桌面会话中枚举顶层窗口、获取窗口图标及 DWM 实时预览、注册用户选择的全局快捷键、恢复或最小化目标窗口，并提供系统托盘常驻入口，因此需要 runFullTrust。应用以当前用户权限运行，不请求管理员提升或 uiAccess；配置写入当前用户可写目录。开机启动默认禁用，可由用户在 Windows 启动设置中启用或禁用。

英文审核说明：

> WindowCue is a local desktop window manager. The runFullTrust capability is required to enumerate top-level windows, retrieve icons and DWM previews, register user-selected global hotkeys, restore or minimize target windows, and provide a system tray entry. It runs with the current user's normal privileges, without requesting elevation or uiAccess. Settings are written to a user-writable location. Startup is disabled by default and is controlled by the user in Windows startup settings.

**剩余阻碍**：EN 正式产品身份待提供；已安装包功能兼容性与 WACK 待完成；Store 截图、描述、隐私政策适用性及其他必填资料待准备/核对。中文正式身份和本地编译/MakeAppx 打包已完成，未上传商店。

提交参考：[MSIX 提交清单](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msix/create-app-submission)、[商店详情页字段](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msix/add-and-edit-store-listing-info)、[runFullTrust 能力](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/app-capability-declarations)、[认证与 Microsoft 签名](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msix/app-certification-process)。
