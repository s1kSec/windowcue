param(
    [string]$OutputDirectory,
    [switch]$Strict
)

$ErrorActionPreference = 'Stop'
$nativeRoot = Split-Path -Parent $PSScriptRoot
$projectRoot = Split-Path -Parent $nativeRoot
. (Join-Path $projectRoot 'packaging\Tools.ps1')
$toolchain = Get-WindowCueToolchain

if (-not $OutputDirectory) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $runId = [guid]::NewGuid().ToString('N').Substring(0, 8)
    $OutputDirectory = Join-Path $nativeRoot "artifacts\icon-visual\$stamp-$runId"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$buildDirectory = Join-Path $OutputDirectory 'build'
New-Item -ItemType Directory -Force -Path $OutputDirectory,$buildDirectory | Out-Null

$resource = Join-Path $buildDirectory 'WindowCueNative.res'
$resourceArguments = @('/nologo', "/fo$resource")
$resourceArguments += $toolchain.IncludePaths | ForEach-Object { "/i$_" }
$resourceArguments += Join-Path $nativeRoot 'WindowCueNative.rc'
Push-Location $nativeRoot
try {
    & $toolchain.Rc @resourceArguments
    if ($LASTEXITCODE -ne 0) { throw "Icon resource build failed: $LASTEXITCODE" }
} finally { Pop-Location }

$executable = Join-Path $buildDirectory 'IconVisual.exe'
$compileArguments = @(
    '/nologo', '/O2', '/std:c++17', '/EHsc', '/MT', '/W4', '/permissive-', '/utf-8',
    '/DUNICODE', '/D_UNICODE', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/D_WIN32_WINNT=0x0A00',
    "/I$(Join-Path $nativeRoot 'src')"
)
$compileArguments += $toolchain.IncludePaths | ForEach-Object { "/I$_" }
$compileArguments += @(
    (Join-Path $PSScriptRoot 'IconVisual.cpp'),
    (Join-Path $nativeRoot 'src\SvgIconRenderer.cpp'),
    $resource, "/Fe:$executable", '/link', '/INCREMENTAL:NO', '/SUBSYSTEM:CONSOLE,6.02'
)
$compileArguments += $toolchain.LibraryPaths | ForEach-Object { "/LIBPATH:$_" }
$compileArguments += @(
    'user32.lib', 'gdi32.lib', 'shlwapi.lib', 'ole32.lib',
    'd2d1.lib', 'd3d11.lib', 'dxgi.lib', 'msimg32.lib', 'windowscodecs.lib'
)
Push-Location $buildDirectory
try {
    & $toolchain.Cl @compileArguments
    if ($LASTEXITCODE -ne 0) { throw "Icon visual test build failed: $LASTEXITCODE" }
} finally { Pop-Location }

$runArguments = @('--out', $OutputDirectory)
if ($Strict) { $runArguments += '--strict' }
& $executable @runArguments
if ($LASTEXITCODE -ne 0) { throw "Icon visual test failed: $LASTEXITCODE. Inspect $OutputDirectory" }
Write-Host "Actual Direct2D icon PNGs, title strip samples and pixel measurements: $OutputDirectory"
