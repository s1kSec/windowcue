[CmdletBinding()]
param(
    [ValidateSet('CN', 'EN', 'All')][string]$Language = 'All',
    [string]$SdkVersion,
    [string]$VisualStudioRoot,
    [switch]$RunSelfTests,
    [switch]$RunStartupTest
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path -Parent $PSScriptRoot
$nativeRoot = Join-Path $projectRoot 'WindowCue.Native'
$packagingRoot = Join-Path $projectRoot 'packaging'
. (Join-Path $packagingRoot 'Tools.ps1')
$tools = Get-WindowCueToolchain -SdkVersion $SdkVersion -VisualStudioRoot $VisualStudioRoot
$config = Get-Content -LiteralPath (Join-Path $packagingRoot 'partner-center.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$version = [string]$config.Version
if ($version -notmatch '^([1-9][0-9]*)\.([0-9]+)\.([0-9]+)\.0$' -or
    @($version.Split('.') | Where-Object { [long]$_ -gt 65535 }).Count -gt 0) {
    throw 'The canonical Store version must be Major.Minor.Build.0 with each component <= 65535.'
}
$runId = '{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss'), ([guid]::NewGuid().ToString('N').Substring(0, 8))
$runRoot = Join-Path $projectRoot "dist\release\$runId"
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$languages = if ($Language -eq 'All') { @('CN', 'EN') } else { @($Language) }
$reports = @()
$hashLines = @()

foreach ($variant in $languages) {
    $variantRoot = Join-Path $runRoot $variant
    $objRoot = Join-Path $variantRoot 'obj'
    $stageRoot = Join-Path $variantRoot 'stage'
    New-Item -ItemType Directory -Path $objRoot, $stageRoot -Force | Out-Null
    $exe = Join-Path $stageRoot 'WindowCue.exe'
    $nativeResource = Join-Path $objRoot 'WindowCueNative.res'
    $versionResource = Join-Path $objRoot 'Version.res'
    $versionRc = Join-Path $objRoot 'Version.rc'
    $tuple = $version.Replace('.', ',')
    $versionText = @"
#include <windows.h>
1 VERSIONINFO
 FILEVERSION $tuple
 PRODUCTVERSION $tuple
 FILEFLAGSMASK 0x3fL
 FILEFLAGS 0x0L
 FILEOS VOS_NT_WINDOWS32
 FILETYPE VFT_APP
 FILESUBTYPE 0x0L
BEGIN
 BLOCK "StringFileInfo"
 BEGIN
  BLOCK "040904b0"
  BEGIN
   VALUE "FileDescription", "WindowCue\0"
   VALUE "FileVersion", "$version\0"
   VALUE "ProductName", "WindowCue\0"
   VALUE "ProductVersion", "$version\0"
   VALUE "OriginalFilename", "WindowCue.exe\0"
  END
 END
 BLOCK "VarFileInfo"
 BEGIN
  VALUE "Translation", 0x409, 1200
 END
END
"@
    [IO.File]::WriteAllText($versionRc, $versionText, [Text.UTF8Encoding]::new($false))
    $rcArgs = @('/nologo') + @($tools.IncludePaths | ForEach-Object { "/i$_" })
    Push-Location $nativeRoot
    try {
        Invoke-WindowCueTool -Path $tools.Rc -Arguments ($rcArgs + @("/fo$nativeResource", 'WindowCueNative.rc')) -LogPath (Join-Path $variantRoot 'resource.log')
        Invoke-WindowCueTool -Path $tools.Rc -Arguments ($rcArgs + @("/fo$versionResource", $versionRc)) -LogPath (Join-Path $variantRoot 'version-resource.log')
        # Match the Store build: native C++17, optimized x64, static CRT and embedded assets.
        $compileArgs = @('/nologo', '/O2', '/MT', '/std:c++17', '/EHsc', '/W4', '/permissive-', '/utf-8', '/DNDEBUG',
            '/DUNICODE', '/D_UNICODE', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/D_WIN32_WINNT=0x0A00', "/DLANG_$variant")
        $compileArgs += @($tools.IncludePaths | ForEach-Object { "/I$_" })
        $compileArgs += @("/I$(Join-Path $nativeRoot 'src')", "/Fo$($objRoot)\", "/Fe:$exe")
        $compileArgs += @('src\Main.cpp', 'src\Platform.cpp', 'src\SvgIconRenderer.cpp', $nativeResource, $versionResource)
        $compileArgs += @('/link', '/MACHINE:X64', '/INCREMENTAL:NO', '/SUBSYSTEM:WINDOWS,6.02', '/DYNAMICBASE', '/NXCOMPAT')
        $compileArgs += @($tools.LibraryPaths | ForEach-Object { "/LIBPATH:$_" })
        $compileArgs += @('user32.lib', 'gdi32.lib', 'shell32.lib', 'shlwapi.lib', 'ole32.lib', 'dwmapi.lib',
            'd2d1.lib', 'd3d11.lib', 'dxgi.lib', 'msimg32.lib', 'uxtheme.lib',
            'advapi32.lib', 'runtimeobject.lib', 'windowsapp.lib')
        $previousPath = $env:PATH
        try {
            $env:PATH = (Split-Path -Parent $tools.Cl) + ';' + $previousPath
            Invoke-WindowCueTool -Path $tools.Cl -Arguments $compileArgs -LogPath (Join-Path $variantRoot 'compile.log')
        } finally { $env:PATH = $previousPath }
    } finally { Pop-Location }

    $dumpOutput = & $tools.Dumpbin /nologo /dependents $exe
    if ($LASTEXITCODE -ne 0) { throw 'dumpbin dependency inspection failed.' }
    $dumpOutput | Set-Content -LiteralPath (Join-Path $variantRoot 'dependencies.txt') -Encoding UTF8
    $imports = @($dumpOutput | ForEach-Object {
        if ($_ -match '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') { $Matches[1] }
    } | Sort-Object -Unique)
    $allowedSystemDlls = @('KERNEL32.dll', 'USER32.dll', 'GDI32.dll', 'SHELL32.dll', 'SHLWAPI.dll', 'ole32.dll',
        'DWMAPI.dll', 'd2d1.dll', 'd3d11.dll', 'dxgi.dll', 'MSIMG32.dll', 'UxTheme.dll', 'ADVAPI32.dll',
        'OLEAUT32.dll', 'COMDLG32.dll', 'COMCTL32.dll', 'COMBASE.dll',
        'api-ms-win-core-winrt-l1-1-0.dll', 'api-ms-win-core-winrt-error-l1-1-0.dll',
        'api-ms-win-core-winrt-string-l1-1-0.dll',
        # windowsapp.lib imports Windows API-set contracts supplied by the OS.
        # Keep the observed contract names explicit; never permit arbitrary DLLs.
        'api-ms-win-core-processenvironment-l1-1-0.dll', 'api-ms-win-core-file-l1-1-0.dll',
        'api-ms-win-core-handle-l1-1-0.dll', 'api-ms-win-core-errorhandling-l1-1-0.dll',
        'api-ms-win-core-synch-l1-1-0.dll', 'api-ms-win-core-processthreads-l1-1-0.dll',
        'api-ms-win-core-libraryloader-l1-2-0.dll', 'api-ms-win-core-heap-l2-1-0.dll',
        'api-ms-win-core-largeinteger-l1-1-0.dll', 'api-ms-win-core-synch-l1-2-0.dll',
        'api-ms-win-core-processthreads-l1-1-1.dll', 'api-ms-win-core-psapi-l1-1-0.dll',
        'api-ms-win-core-string-l1-1-0.dll', 'api-ms-win-appmodel-runtime-l1-1-0.dll',
        'api-ms-win-core-libraryloader-l1-2-1.dll', 'api-ms-win-core-rtlsupport-l1-1-0.dll',
        'api-ms-win-core-interlocked-l1-1-0.dll', 'api-ms-win-core-heap-l1-1-0.dll',
        'api-ms-win-core-console-l1-1-0.dll', 'api-ms-win-core-fibers-l1-1-0.dll',
        'api-ms-win-core-localization-l1-2-0.dll')
    if ($imports.Count -eq 0) { throw 'No PE imports detected; dependency inspection cannot be trusted.' }
    foreach ($dll in $imports) {
        if ($dll -notin $allowedSystemDlls) { throw "Unexpected runtime dependency $dll; audit it before shipping." }
    }
    $headers = & $tools.Dumpbin /nologo /headers $exe
    if ($LASTEXITCODE -ne 0 -or -not ($headers -match '8664 machine')) { throw 'Compiled EXE is not verified x64.' }
    $headers | Set-Content -LiteralPath (Join-Path $variantRoot 'pe-headers.txt') -Encoding UTF8
    if ((Get-Item -LiteralPath $exe).VersionInfo.FileVersion -ne $version) { throw 'EXE version resource mismatch.' }

    # Keep the policy byte-for-byte intact and retain its relative audit link.
    Copy-Item -LiteralPath (Join-Path $packagingRoot 'PRIVACY.md') -Destination $stageRoot
    Copy-Item -LiteralPath (Join-Path $packagingRoot 'PRIVACY-AUDIT.md') -Destination $stageRoot
    $instructions = if ($variant -eq 'CN') {
@"
WindowCue $version - Windows x64 中文便携版

系统要求：Windows 10 1809（17763）或更高版本 x64、Windows 11 x64。
完整解压 ZIP 后双击 WindowCue.exe，无需另行安装 .NET 或 VC++ 运行库。
独立版 EXE 未签名；不包含商店 MSIX 或本地测试签名证书。

选择窗口卡片，在底部点击快捷键输入区，按带修饰键的组合并点击“绑定”。
目标窗口在前台时，快捷键将其最小化；否则恢复并切换到该窗口。
首次运行默认 Alt+P 唤出或隐藏 WindowCue；已有自定义绑定优先，已取消的不再自动绑定。
启动后静默驻留托盘；按 Alt+P 或双击托盘图标打开界面。程序未运行时可用 WindowCue.exe --show 启动并显示界面。
首次正常运行默认启用当前用户登录时自启动。在主题按钮左侧的齿轮设置中可自由开关。
若 Windows 已禁用启动项，请在 Windows“启动应用”中重新启用；应用不会覆盖系统禁用状态。
启用后请保留解压目录，移动程序后需从新位置运行以更新启动路径；删除前建议关闭自启动。
关闭或最小化窗口会进入托盘；双击托盘图标恢复，右键托盘选择“退出”。
点击标题栏太阳/月亮按钮切换主题，齿轮设置中的 GitHub 图标打开项目仓库。

“便携版”指无需安装的独立程序；默认配置仍保存于 %LOCALAPPDATA%\WindowCue。
请阅读同目录 PRIVACY.md。反馈：https://github.com/s1kSec/windowcue/issues
下载更新：https://github.com/s1kSec/windowcue/releases
"@
    } else {
@"
WindowCue $version - Windows x64 English portable edition

Requirements: Windows 10 version 1809 (build 17763) or later x64, or Windows 11 x64.
Extract the complete ZIP and launch WindowCue.exe. No separate .NET or VC++ runtime installation is required.
This standalone EXE is unsigned. No Store MSIX or local test-signing certificate is included.

Select a window card, click the shortcut input at the bottom, press a modifier-key combination, and click Bind.
The shortcut minimizes a foreground target, or restores and switches to it otherwise.
Alt+P shows/hides WindowCue on first use. Existing custom bindings take priority; a canceled binding stays canceled.
Launch starts silently in the tray. Press Alt+P or double-click the tray icon to open the UI. When not already running, WindowCue.exe --show starts with the UI visible.
The first normal launch enables startup at current-user sign-in. Toggle it in the gear to the left of the theme button.
If Windows has disabled startup, re-enable it in Windows Startup apps; the app respects the system-disabled state.
Keep the extracted folder while startup is enabled. Launch from a new location after moving it; disable startup before deleting it.
Closing or minimizing moves the app to the tray. Double-click its icon to restore; use the tray Exit command to quit.
Click the sun/moon button to change the theme; the GitHub icon in settings opens the project repository.

Portable means no installer is required; settings are saved in %LOCALAPPDATA%\WindowCue by default.
Read PRIVACY.md in this folder. Feedback: https://github.com/s1kSec/windowcue/issues
Updates: https://github.com/s1kSec/windowcue/releases
"@
    }
    [IO.File]::WriteAllText((Join-Path $stageRoot 'README.txt'), $instructions, [Text.UTF8Encoding]::new($true))
    $zipName = "WindowCue-$version-windows-x64-$variant.zip"
    $zip = Join-Path $runRoot $zipName
    Compress-Archive -LiteralPath @(Get-ChildItem -LiteralPath $stageRoot -File | ForEach-Object FullName) -DestinationPath $zip -CompressionLevel Optimal
    $extractRoot = Join-Path $variantRoot 'verification-extracted'
    Expand-Archive -LiteralPath $zip -DestinationPath $extractRoot
    $extractedExe = Join-Path $extractRoot 'WindowCue.exe'
    if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $extractedExe -Algorithm SHA256).Hash) { throw 'ZIP EXE checksum mismatch.' }
    $expectedFiles = @('WindowCue.exe', 'README.txt', 'PRIVACY.md', 'PRIVACY-AUDIT.md')
    $actualFiles = @(Get-ChildItem -LiteralPath $extractRoot -File | ForEach-Object Name)
    if (@(Compare-Object $expectedFiles $actualFiles).Count -ne 0) { throw 'ZIP contents differ from the standalone distribution allowlist.' }

    $tests = @()
    if ($RunSelfTests) {
        $oldSettings = [Environment]::GetEnvironmentVariable('WINDOWCUE_SETTINGS_DIR', 'Process')
        try {
            $env:WINDOWCUE_SETTINGS_DIR = Join-Path $variantRoot 'self-test-settings'
            New-Item -ItemType Directory -Path $env:WINDOWCUE_SETTINGS_DIR -Force | Out-Null
            foreach ($test in @('--binding-self-test', '--svg-self-test', '--startup-self-test')) {
                $process = Start-Process -FilePath $extractedExe -ArgumentList $test -WindowStyle Hidden -PassThru
                if (-not $process.WaitForExit(30000)) { $process.Kill(); throw "$test timed out." }
                $tests += [pscustomobject]@{ Test = $test; ExitCode = $process.ExitCode; Passed = ($process.ExitCode -eq 0) }
                if ($process.ExitCode -ne 0) { throw "$test failed with exit code $($process.ExitCode)." }
            }
        } finally { [Environment]::SetEnvironmentVariable('WINDOWCUE_SETTINGS_DIR', $oldSettings, 'Process') }
    }
    if ($RunStartupTest) {
        $tests += & (Join-Path $PSScriptRoot 'Test-PortableStartup.ps1') -Executable $extractedExe -SettingsDirectory (Join-Path $variantRoot 'startup-test-settings')
    }
    $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
    $hashLines += "$hash  $zipName"
    $reports += [ordered]@{
        Version = $version; Language = $variant; Configuration = 'Release'; Architecture = 'x64'; Stack = 'C++17/Win32'
        Compiler = $tools.MsvcVersion; WindowsSdk = $tools.SdkVersion; Runtime = 'Static CRT (/MT); Windows system DLLs only'
        ImportedSystemDlls = $imports; ExternalRuntimeDependencies = @(); Signature = 'Unsigned standalone EXE'
        FileName = $zipName; Bytes = (Get-Item -LiteralPath $zip).Length; SHA256 = $hash
        ExecutableSHA256 = (Get-FileHash -LiteralPath $extractedExe -Algorithm SHA256).Hash
        Contents = $actualFiles; Tests = $tests; ZipExtractionHashMatch = $true
    }
    Write-Host "Portable ZIP: $zip"
    Write-Host "SHA256: $hash"
}
$hashLines | Set-Content -LiteralPath (Join-Path $runRoot 'SHA256SUMS.txt') -Encoding ASCII
[ordered]@{ Version = $version; BuiltAt = (Get-Date).ToString('o'); Artifacts = $reports } |
    ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'report.json') -Encoding UTF8
[pscustomobject]@{ Version = $version; OutputDirectory = $runRoot; Artifacts = $reports }
