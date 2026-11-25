param([switch]$BuildOnly)
$ErrorActionPreference = 'Stop'
$nativeRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path (Split-Path -Parent $nativeRoot) 'packaging\Tools.ps1')
$toolchain = Get-WindowCueToolchain
$runRoot = Join-Path $nativeRoot ('artifacts\vector-preview\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8))
New-Item -ItemType Directory -Path $runRoot | Out-Null
$argsList = @('/nologo','/O2','/std:c++17','/EHsc','/MT','/W4','/permissive-','/utf-8','/DUNICODE','/D_UNICODE','/DWIN32_LEAN_AND_MEAN','/DNOMINMAX','/D_WIN32_WINNT=0x0A00')
$argsList += $toolchain.IncludePaths | ForEach-Object { "/I$_" }
$argsList += @((Join-Path $PSScriptRoot 'VectorPreview.cpp'), "/Fe:$runRoot\VectorPreview.exe", '/link','/INCREMENTAL:NO','user32.lib','gdi32.lib','dwmapi.lib','ole32.lib')
$argsList += $toolchain.LibraryPaths | ForEach-Object { "/LIBPATH:$_" }
Push-Location $runRoot
try { & $toolchain.Cl @argsList; if($LASTEXITCODE -ne 0) { throw 'Vector preview build failed.' } } finally { Pop-Location }
Write-Output "VECTOR_PREVIEW_EXE=$runRoot\VectorPreview.exe"
if(-not $BuildOnly) {
    & (Join-Path $runRoot 'VectorPreview.exe') | Tee-Object -FilePath (Join-Path $runRoot 'result.txt')
    if($LASTEXITCODE -ne 0) { throw "Vector preview failed: $LASTEXITCODE" }
}
