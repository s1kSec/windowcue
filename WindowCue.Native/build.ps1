param([switch]$BuildRegressionTests)

$ErrorActionPreference = 'Stop'

$nativeRoot = $PSScriptRoot
$projectRoot = Split-Path -Parent $nativeRoot
$outputRoot = Join-Path $projectRoot 'dist\WindowCue.Native'
$objectRoot = Join-Path $nativeRoot 'artifacts\obj'
$sourceFiles = @(
    (Join-Path $nativeRoot 'src\Main.cpp'),
    (Join-Path $nativeRoot 'src\Platform.cpp'),
    (Join-Path $nativeRoot 'src\SvgIconRenderer.cpp')
)

function Find-VisualStudio {
    $vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        return $null
    }
    $installation = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $installation) {
        return $null
    }
    return $installation
}

$visualStudio = Find-VisualStudio
if (-not $visualStudio) {
    throw 'MSVC C++ Build Tools were not found. Install the Visual Studio C++ x64 build tools and rerun this script.'
}

$msvcRoot = Join-Path $visualStudio 'VC\Tools\MSVC'
$msvcVersion = Get-ChildItem -LiteralPath $msvcRoot -Directory |
    Sort-Object { [version]$_.Name } -Descending |
    Select-Object -First 1
if (-not $msvcVersion) { throw "No MSVC toolset was found under $msvcRoot" }

$cl = Join-Path $msvcVersion.FullName 'bin\Hostx64\x64\cl.exe'
$msvcInclude = Join-Path $msvcVersion.FullName 'include'
$msvcLib = Join-Path $msvcVersion.FullName 'lib\x64'
$sdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
$sdkVersion = Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'Include') -Directory |
    Where-Object { $_.Name -match '^10\.\d+\.\d+\.\d+$' } |
    Sort-Object { [version]$_.Name } -Descending |
    Select-Object -First 1
if (-not $sdkVersion) { throw "No Windows SDK was found under $sdkRoot" }
$sdkName = $sdkVersion.Name
$rc = Join-Path $sdkRoot "bin\$sdkName\x64\rc.exe"
$sdkUcrtInclude = Join-Path $sdkRoot "Include\$sdkName\ucrt"
$sdkSharedInclude = Join-Path $sdkRoot "Include\$sdkName\shared"
$sdkUmInclude = Join-Path $sdkRoot "Include\$sdkName\um"
$sdkWinRtInclude = Join-Path $sdkRoot "Include\$sdkName\winrt"
$sdkCppWinRtInclude = Join-Path $sdkRoot "Include\$sdkName\cppwinrt"
$sdkUcrtLib = Join-Path $sdkRoot "Lib\$sdkName\ucrt\x64"
$sdkUmLib = Join-Path $sdkRoot "Lib\$sdkName\um\x64"

New-Item -ItemType Directory -Force -Path $outputRoot,$objectRoot | Out-Null

function Build-NativeVariant {
    param(
        [Parameter(Mandatory = $true)][string]$Suffix,
        [Parameter(Mandatory = $true)][string]$LanguageDefine,
        [string[]]$VariantSources = $sourceFiles,
        [string]$Destination = $outputRoot
    )

    $variantRoot = Join-Path $objectRoot $Suffix
    New-Item -ItemType Directory -Force -Path $variantRoot | Out-Null
    $resource = Join-Path $variantRoot 'WindowCueNative.res'
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    $executable = Join-Path $Destination "WindowCue_Native_$Suffix.exe"
    $resourceArgs = @(
        '/nologo', "/i$msvcInclude", "/i$sdkUcrtInclude", "/i$sdkSharedInclude", "/i$sdkUmInclude",
        "/fo$resource", (Join-Path $nativeRoot 'WindowCueNative.rc')
    )
    & $rc @resourceArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Resource build $Suffix failed with exit code $LASTEXITCODE"
    }

    $compileArgs = @(
        '/nologo', '/O2', '/std:c++17', '/EHsc', '/MT', '/W4', '/permissive-', '/utf-8',
        '/DUNICODE', '/D_UNICODE', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX', '/D_WIN32_WINNT=0x0A00',
        $LanguageDefine,
        "/I$(Join-Path $nativeRoot 'src')", "/I$msvcInclude", "/I$sdkUcrtInclude",
        "/I$sdkSharedInclude", "/I$sdkUmInclude", "/I$sdkWinRtInclude", "/I$sdkCppWinRtInclude"
    )
    $compileArgs += $VariantSources
    $compileArgs += @(
        $resource, "/Fe:$executable", '/link', '/INCREMENTAL:NO', '/SUBSYSTEM:WINDOWS,6.02',
        "/LIBPATH:$msvcLib", "/LIBPATH:$sdkUcrtLib", "/LIBPATH:$sdkUmLib",
        'user32.lib', 'gdi32.lib', 'shell32.lib', 'shlwapi.lib', 'ole32.lib', 'dwmapi.lib',
        'd2d1.lib', 'd3d11.lib', 'dxgi.lib', 'msimg32.lib', 'uxtheme.lib',
        'advapi32.lib', 'runtimeobject.lib', 'windowsapp.lib'
    )
    & $cl @compileArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Native build $Suffix failed with exit code $LASTEXITCODE"
    }
    Write-Host "Published: $executable"
}

Build-NativeVariant -Suffix 'CN' -LanguageDefine '/DLANG_CN'
Build-NativeVariant -Suffix 'EN' -LanguageDefine '/DLANG_EN'

if ($BuildRegressionTests) {
    $testSources = @(
        (Join-Path $nativeRoot 'tests\UiRegression.cpp'),
        (Join-Path $nativeRoot 'src\Platform.cpp'),
        (Join-Path $nativeRoot 'src\SvgIconRenderer.cpp')
    )
    $testOutput = Join-Path $nativeRoot 'artifacts\tests'
    Build-NativeVariant -Suffix 'Regression_CN' -LanguageDefine '/DLANG_CN' `
        -VariantSources $testSources -Destination $testOutput
    Build-NativeVariant -Suffix 'Regression_EN' -LanguageDefine '/DLANG_EN' `
        -VariantSources $testSources -Destination $testOutput
}
