[CmdletBinding()]
param(
    [ValidateSet('CN', 'EN')][string]$Language = 'CN',
    [ValidateSet('Store', 'LocalTest')][string]$Mode = 'Store',
    [string]$IdentityConfig,
    [string]$Version,
    [string]$SdkVersion,
    [string]$VisualStudioRoot,
    [switch]$RunSelfTests,
    [switch]$CheckToolsOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Tools.ps1')
$tools = Get-WindowCueToolchain -SdkVersion $SdkVersion -VisualStudioRoot $VisualStudioRoot
if ($CheckToolsOnly) { $tools | ConvertTo-Json -Depth 4; return }

$projectRoot = Split-Path -Parent $PSScriptRoot
$nativeRoot = Join-Path $projectRoot 'WindowCue.Native'
if (-not (Test-Path -LiteralPath (Join-Path $nativeRoot 'src\Main.cpp'))) {
    throw 'Expected the current C++/Win32 WindowCue.Native source tree. No fallback to the .NET branch.'
}
if (-not $IdentityConfig) { $IdentityConfig = Join-Path $PSScriptRoot 'partner-center.json' }
$config = Get-Content -LiteralPath $IdentityConfig -Raw -Encoding UTF8 | ConvertFrom-Json
if (-not $Version) { $Version = [string]$config.Version }
if ($Version -notmatch '^([1-9][0-9]*)\.([0-9]+)\.([0-9]+)\.0$') {
    throw 'Store-compatible version required: Major.Minor.Build.0 (Major >= 1).'
}
foreach ($part in $Version.Split('.')) {
    if ([long]$part -gt 65535) { throw 'Each version component must be <= 65535.' }
}
$identity = $config.$Language
if ($Mode -eq 'Store') {
    foreach ($field in @('IdentityName', 'Publisher', 'PublisherDisplayName', 'DisplayName')) {
        if ([string]::IsNullOrWhiteSpace([string]$identity.$field) -or
            [string]$identity.$field -match '__|TODO|REPLACE|<|>') {
            throw "Missing real Partner Center $Language/$field in $IdentityConfig. No formal identity will be invented."
        }
        if ([string]$identity.$field -ne ([string]$identity.$field).Trim()) {
            throw "Remove accidental leading/trailing whitespace from $Language/$field."
        }
    }
    if ($config.CN.IdentityName -and $config.EN.IdentityName -and
        $config.CN.IdentityName -eq $config.EN.IdentityName -and
        $config.CN.Publisher -eq $config.EN.Publisher) {
        throw 'Independent CN/EN products must have different Partner Center identities.'
    }
} else {
    $identity = [pscustomobject]@{
        IdentityName = "WindowCue.LocalTest.$Language"
        Publisher = 'CN=WindowCue Local Test'
        PublisherDisplayName = 'WindowCue Local Test'
        DisplayName = "WindowCue $Language (Local Test)"
        ExpectedPackageFamilyName = $null
        StoreId = $null
    }
}
if ($identity.IdentityName -notmatch '^[A-Za-z0-9][A-Za-z0-9.-]{2,49}$') {
    throw 'Identity Name must be 3-50 ASCII letters, digits, periods or hyphens.'
}
if ($identity.Publisher -notmatch '^CN=') { throw 'Publisher must be the complete Partner Center distinguished name, starting with CN=.' }

# Never clean existing artifacts or modify the standalone dist directory.
$runId = '{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss'), ([guid]::NewGuid().ToString('N').Substring(0, 8))
$runRoot = Join-Path $PSScriptRoot "artifacts\$Mode-$Language-$runId"
$objRoot = Join-Path $runRoot 'obj'
$stageRoot = Join-Path $runRoot 'stage'
$appRoot = Join-Path $stageRoot 'app'
$outputRoot = Join-Path $PSScriptRoot 'output'
New-Item -ItemType Directory -Force -Path $objRoot, $appRoot, $outputRoot | Out-Null
Write-Host "Stack: C++17/Win32; Release /O2 /MT; x64; SDK $($tools.SdkVersion)"
Write-Host "Identity: $($identity.IdentityName); mode: $Mode; language: $Language"

& (Join-Path $PSScriptRoot 'New-Assets.ps1') | ForEach-Object { Write-Host $_ }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Assets') -Destination $stageRoot -Recurse
$exe = Join-Path $appRoot 'WindowCue.exe'
$nativeResource = Join-Path $objRoot 'WindowCueNative.res'
$versionResource = Join-Path $objRoot 'Version.res'
$versionRc = Join-Path $objRoot 'Version.rc'
$tuple = $Version.Replace('.', ',')
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
   VALUE "FileVersion", "$Version\0"
   VALUE "ProductName", "WindowCue\0"
   VALUE "ProductVersion", "$Version\0"
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
$rcArgs = @('/nologo')
$rcArgs += @($tools.IncludePaths | ForEach-Object { "/i$_" })
Push-Location $nativeRoot
try {
    Invoke-WindowCueTool -Path $tools.Rc -Arguments ($rcArgs + @("/fo$nativeResource", 'WindowCueNative.rc')) -LogPath (Join-Path $runRoot 'resource.log')
    Invoke-WindowCueTool -Path $tools.Rc -Arguments ($rcArgs + @("/fo$versionResource", $versionRc)) -LogPath (Join-Path $runRoot 'version-resource.log')
    $compileArgs = @('/nologo', '/O2', '/MT', '/std:c++17', '/EHsc', '/W4', '/permissive-', '/utf-8', '/DNDEBUG',
        '/DUNICODE', '/D_UNICODE', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/D_WIN32_WINNT=0x0A00', "/DLANG_$Language")
    $compileArgs += @($tools.IncludePaths | ForEach-Object { "/I$_" })
    $compileArgs += @("/I$(Join-Path $nativeRoot 'src')", "/Fo$($objRoot)\", "/Fe:$exe")
    $compileArgs += @('src\Main.cpp', 'src\Platform.cpp', 'src\SvgIconRenderer.cpp', $nativeResource, $versionResource)
    $compileArgs += @('/link', '/MACHINE:X64', '/INCREMENTAL:NO', '/SUBSYSTEM:WINDOWS,6.02', '/DYNAMICBASE', '/NXCOMPAT')
    $compileArgs += @($tools.LibraryPaths | ForEach-Object { "/LIBPATH:$_" })
    $compileArgs += @('user32.lib', 'gdi32.lib', 'shell32.lib', 'shlwapi.lib', 'ole32.lib', 'dwmapi.lib',
        'd2d1.lib', 'd3d11.lib', 'dxgi.lib', 'msimg32.lib', 'uxtheme.lib',
        'advapi32.lib', 'runtimeobject.lib', 'windowsapp.lib')
    # cl invokes link; supply only its tool directory temporarily and restore PATH.
    $previousPath = $env:PATH
    try {
        $env:PATH = (Split-Path -Parent $tools.Cl) + ';' + $previousPath
        Invoke-WindowCueTool -Path $tools.Cl -Arguments $compileArgs -LogPath (Join-Path $runRoot 'compile.log')
    } finally { $env:PATH = $previousPath }
} finally { Pop-Location }

# /MT means no VC redistributable or .NET package dependency. Fail on unexpected imports.
$dumpOutput = & $tools.Dumpbin /nologo /dependents $exe
if ($LASTEXITCODE -ne 0) { throw 'dumpbin dependency inspection failed.' }
$dumpOutput | Set-Content -LiteralPath (Join-Path $runRoot 'dependencies.txt') -Encoding UTF8
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
    if ($dll -notin $allowedSystemDlls) {
        throw "Unexpected runtime dependency $dll. Audit and collect it before shipping; do not copy arbitrary System32 DLLs."
    }
}
$headers = & $tools.Dumpbin /nologo /headers $exe
if ($LASTEXITCODE -ne 0 -or -not ($headers -match '8664 machine')) { throw 'Compiled EXE is not verified x64.' }
$headers | Set-Content -LiteralPath (Join-Path $runRoot 'pe-headers.txt') -Encoding UTF8
$selfTests = @()
if ($RunSelfTests) {
    $oldSettingsDirectory = [Environment]::GetEnvironmentVariable('WINDOWCUE_SETTINGS_DIR', 'Process')
    try {
        $env:WINDOWCUE_SETTINGS_DIR = Join-Path $runRoot 'test-settings'
        New-Item -ItemType Directory -Force -Path $env:WINDOWCUE_SETTINGS_DIR | Out-Null
        foreach ($test in @('--binding-self-test', '--svg-self-test', '--startup-self-test')) {
            $process = Start-Process -FilePath $exe -ArgumentList $test -WindowStyle Hidden -Wait -PassThru
            $selfTests += [pscustomobject]@{ Test = $test; ExitCode = $process.ExitCode }
            if ($process.ExitCode -ne 0) { throw "$test failed: $($process.ExitCode)" }
        }
    } finally { [Environment]::SetEnvironmentVariable('WINDOWCUE_SETTINGS_DIR', $oldSettingsDirectory, 'Process') }
}
$description = if ($Language -eq 'CN') { 'Window switching and global hotkey utility (Chinese interface).' } else { 'Window switching and global hotkey utility (English interface).' }
$values = @{
    '__IDENTITY_NAME__' = $identity.IdentityName
    '__PUBLISHER__' = $identity.Publisher
    '__VERSION__' = $Version
    '__DISPLAY_NAME__' = $identity.DisplayName
    '__PUBLISHER_DISPLAY_NAME__' = $identity.PublisherDisplayName
    '__DESCRIPTION__' = $description
    '__LANGUAGE__' = $(if ($Language -eq 'CN') { 'zh-cn' } else { 'en-us' })
    '__SDK_VERSION__' = $tools.SdkVersion
}
$manifestText = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'AppxManifest.xml') -Raw -Encoding UTF8
foreach ($key in $values.Keys) { $manifestText = $manifestText.Replace($key, [Security.SecurityElement]::Escape([string]$values[$key])) }
if ($manifestText -match '__[A-Z_]+__') { throw 'Unresolved manifest placeholder.' }
$manifestPath = Join-Path $stageRoot 'AppxManifest.xml'
[IO.File]::WriteAllText($manifestPath, $manifestText, [Text.UTF8Encoding]::new($false))

function Assert-WindowCueStartupManifest {
    param([Parameter(Mandatory = $true)][xml]$Manifest)
    $namespaces = [Xml.XmlNamespaceManager]::new($Manifest.NameTable)
    $namespaces.AddNamespace('foundation', 'http://schemas.microsoft.com/appx/manifest/foundation/windows10')
    $namespaces.AddNamespace('desktop', 'http://schemas.microsoft.com/appx/manifest/desktop/windows10')
    $extensions = @($Manifest.SelectNodes('/foundation:Package/foundation:Applications/foundation:Application/foundation:Extensions/desktop:Extension[@Category="windows.startupTask"]', $namespaces))
    if ($extensions.Count -ne 1 -or $extensions[0].Executable -cne 'app\WindowCue.exe' -or
        $extensions[0].EntryPoint -cne 'Windows.FullTrustApplication') {
        throw 'Manifest requires exactly one full-trust WindowCue startup extension.'
    }
    $tasks = @($extensions[0].SelectNodes('desktop:StartupTask', $namespaces))
    if ($tasks.Count -ne 1 -or $tasks[0].TaskId -cne 'WindowCueStartup' -or
        $tasks[0].Enabled -cne 'true' -or [string]::IsNullOrWhiteSpace($tasks[0].DisplayName)) {
        throw 'Manifest startup task must match the backend task ID and be enabled by default.'
    }
}
[xml]$stagedManifest = $manifestText
Assert-WindowCueStartupManifest -Manifest $stagedManifest

# Index scale/targetsize assets, without modifying the source icons.
$priConfig = Join-Path $runRoot 'priconfig.xml'
Invoke-WindowCueTool -Path $tools.MakePri -Arguments @('createconfig', '/cf', $priConfig, '/dq', $values['__LANGUAGE__'], '/o') -LogPath (Join-Path $runRoot 'pri-config.log')
Invoke-WindowCueTool -Path $tools.MakePri -Arguments @('new', '/pr', $stageRoot, '/cf', $priConfig, '/mn', $manifestPath,
    '/of', (Join-Path $stageRoot 'resources.pri'), '/o') -LogPath (Join-Path $runRoot 'pri.log')
$packageFileName = '{0}_{1}_x64_{2}_{3}.msix' -f $identity.IdentityName, $Version, $Language, $Mode
$packagePath = Join-Path $outputRoot $packageFileName
# Fresh filenames preserve all previous builds, signatures and reports.
if (Test-Path -LiteralPath $packagePath) { $packagePath = Join-Path $outputRoot ($packageFileName.Replace('.msix', "_$runId.msix")) }
Invoke-WindowCueTool -Path $tools.MakeAppx -Arguments @('pack', '/d', $stageRoot, '/p', $packagePath, '/h', 'SHA256', '/o') -LogPath (Join-Path $runRoot 'pack.log')
$inspectionRoot = Join-Path $runRoot 'inspection'
Invoke-WindowCueTool -Path $tools.MakeAppx -Arguments @('unpack', '/p', $packagePath, '/d', $inspectionRoot, '/pfn', '/o') -LogPath (Join-Path $runRoot 'unpack.log')
$unpacked = @(Get-ChildItem -LiteralPath $inspectionRoot -Directory)
if ($unpacked.Count -ne 1) { throw 'Expected one unpacked package.' }
$packageFullName = $unpacked[0].Name
$packageFamilyName = $identity.IdentityName + '_' + $packageFullName.Split('_')[-1]
if ($identity.ExpectedPackageFamilyName -and $packageFamilyName -cne $identity.ExpectedPackageFamilyName) {
    throw "Partner Center PFN mismatch: expected $($identity.ExpectedPackageFamilyName); built $packageFamilyName"
}
$unpackedExe = Join-Path $unpacked[0].FullName 'app\WindowCue.exe'
if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $unpackedExe -Algorithm SHA256).Hash) {
    throw 'Packaged EXE checksum mismatch.'
}
[xml]$unpackedManifest = Get-Content -LiteralPath (Join-Path $unpacked[0].FullName 'AppxManifest.xml') -Raw -Encoding UTF8
if ($unpackedManifest.Package.Identity.Name -cne $identity.IdentityName -or
    $unpackedManifest.Package.Identity.Publisher -cne $identity.Publisher -or
    $unpackedManifest.Package.Identity.Version -ne $Version -or
    $unpackedManifest.Package.Identity.ProcessorArchitecture -ne 'x64') { throw 'Unpacked manifest identity/architecture/version mismatch.' }
Assert-WindowCueStartupManifest -Manifest $unpackedManifest
$report = [ordered]@{
    BuiltAt = (Get-Date).ToString('o'); Mode = $Mode; Language = $Language; Stack = 'C++17/Win32'; Configuration = 'Release'; Architecture = 'x64'
    IdentityName = $identity.IdentityName; Publisher = $identity.Publisher; PublisherDisplayName = $identity.PublisherDisplayName
    DisplayName = $identity.DisplayName; PackageFamilyName = $packageFamilyName; StoreId = $identity.StoreId; Version = $Version
    Tools = $tools; ImportedSystemDlls = $imports; ExternalRuntimeDependencies = @(); SelfTests = $selfTests
    MakeAppxValidation = 'Passed (no /nv)'; UnpackedExeHashMatch = $true; Signature = 'Unsigned; Store re-signs, local installation needs a test signature'
    StartupManifestValidation = 'Passed (staged and unpacked)'; StartupTaskId = 'WindowCueStartup'; StartupEnabledByDefault = $true
    InstalledMsixRuntimeTests = 'Not run'; Wack = 'Not run'; ArtifactsDirectory = $runRoot
    PackagePath = $packagePath; PackageBytes = (Get-Item -LiteralPath $packagePath).Length; SHA256 = (Get-FileHash -LiteralPath $packagePath -Algorithm SHA256).Hash
}
$reportPath = $packagePath + '.report.json'
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8
Write-Host "Package: $packagePath"
Write-Host "Report:  $reportPath"
Write-Host "PFN:     $packageFamilyName"
if ($Mode -eq 'LocalTest') { Write-Warning 'LOCAL TEST IDENTITY: do not submit this package to Partner Center.' }
[pscustomobject]$report
