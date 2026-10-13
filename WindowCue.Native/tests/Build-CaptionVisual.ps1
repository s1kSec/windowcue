param(
    [string]$OutputDirectory,
    [ValidateSet('CN','EN')][string]$Language = 'CN',
    [switch]$BuildOnly
)

$ErrorActionPreference = 'Stop'
$nativeRoot = Split-Path -Parent $PSScriptRoot
$projectRoot = Split-Path -Parent $nativeRoot
. (Join-Path $projectRoot 'packaging\Tools.ps1')
$toolchain = Get-WindowCueToolchain
if (-not $OutputDirectory) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $runId = [guid]::NewGuid().ToString('N').Substring(0, 8)
    $OutputDirectory = Join-Path $nativeRoot "artifacts\caption-visual\$stamp-$runId-$Language"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Use a fresh output directory; visual artifacts are never overwritten.' }
$buildDirectory = Join-Path $OutputDirectory 'build'
$settingsDirectory = Join-Path $OutputDirectory 'isolated-settings'
New-Item -ItemType Directory -Path $OutputDirectory,$buildDirectory,$settingsDirectory | Out-Null

$resource = Join-Path $buildDirectory 'WindowCueNative.res'
$resourceArguments = @('/nologo', "/fo$resource")
$resourceArguments += $toolchain.IncludePaths | ForEach-Object { "/i$_" }
$resourceArguments += Join-Path $nativeRoot 'WindowCueNative.rc'
Push-Location $nativeRoot
try {
    & $toolchain.Rc @resourceArguments
    if ($LASTEXITCODE -ne 0) { throw "Caption resource build failed: $LASTEXITCODE" }
} finally { Pop-Location }

$executable = Join-Path $buildDirectory 'CaptionVisual.exe'
$compileArguments = @(
    '/nologo', '/O2', '/std:c++17', '/EHsc', '/MT', '/W4', '/permissive-', '/utf-8',
    '/DUNICODE', '/D_UNICODE', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/D_WIN32_WINNT=0x0A00',
    "/DLANG_$Language", "/I$(Join-Path $nativeRoot 'src')"
)
$compileArguments += $toolchain.IncludePaths | ForEach-Object { "/I$_" }
$compileArguments += @(
    (Join-Path $PSScriptRoot 'CaptionVisual.cpp'),
    (Join-Path $nativeRoot 'src\Platform.cpp'),
    (Join-Path $nativeRoot 'src\SvgIconRenderer.cpp'),
    $resource, "/Fe:$executable", '/link', '/INCREMENTAL:NO', '/SUBSYSTEM:CONSOLE,6.02'
)
$compileArguments += $toolchain.LibraryPaths | ForEach-Object { "/LIBPATH:$_" }
$compileArguments += @(
    'user32.lib', 'gdi32.lib', 'shell32.lib', 'shlwapi.lib', 'ole32.lib', 'dwmapi.lib',
    'd2d1.lib', 'd3d11.lib', 'dxgi.lib', 'msimg32.lib', 'uxtheme.lib',
    'advapi32.lib', 'runtimeobject.lib', 'windowsapp.lib', 'comctl32.lib', 'windowscodecs.lib'
)
Push-Location $buildDirectory
try {
    & $toolchain.Cl @compileArguments
    if ($LASTEXITCODE -ne 0) { throw "Caption visual build failed: $LASTEXITCODE" }
} finally { Pop-Location }
Write-Host "CAPTION_VISUAL_EXE=$executable"
Write-Host "CAPTION_VISUAL_OUTPUT=$OutputDirectory"
if (-not $BuildOnly) {
    $previousSettings = $env:WINDOWCUE_SETTINGS_DIR
    try {
        $env:WINDOWCUE_SETTINGS_DIR = $settingsDirectory
        & $executable $OutputDirectory
        if ($LASTEXITCODE -ne 0) { throw "Native caption visual check failed: $LASTEXITCODE. Inspect $OutputDirectory" }
    } finally { $env:WINDOWCUE_SETTINGS_DIR = $previousSettings }
}
