# Shared command-line tooling for WindowCue packaging. Compatible with Windows PowerShell 5.1.

function Get-WindowCueToolchain {
    [CmdletBinding()]
    param(
        [string]$SdkVersion,
        [string]$VisualStudioRoot
    )

    $programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
    if (-not $programFilesX86) { $programFilesX86 = ${env:ProgramFiles(x86)} }
    if (-not $programFilesX86) {
        throw 'This script requires x64 Windows with Microsoft C++ Build Tools and the Windows SDK.'
    }
    $vsInstallCommand = 'winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"'
    $sdkInstallCommand = 'winget install --id Microsoft.WindowsSDK.10.0.22621 --exact'

    if (-not $VisualStudioRoot) {
        $vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
            $vsCandidates = @(& $vswhere -latest -products '*' `
                -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                -property installationPath)
            if ($LASTEXITCODE -ne 0) {
                throw "vswhere failed with exit code $LASTEXITCODE. Repair the Visual Studio Installer and rerun."
            }
            $VisualStudioRoot = $vsCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Container) } |
                Select-Object -First 1
        }
        if (-not $VisualStudioRoot) {
            throw "Microsoft C++ x64 Build Tools were not found. Install them with: $vsInstallCommand . Alternatively pass -VisualStudioRoot with an existing Build Tools installation."
        }
    }
    if (-not (Test-Path -LiteralPath $VisualStudioRoot -PathType Container)) {
        throw "Visual Studio installation directory does not exist: $VisualStudioRoot"
    }
    $VisualStudioRoot = (Resolve-Path -LiteralPath $VisualStudioRoot -ErrorAction Stop).ProviderPath
    $msvcRoot = Join-Path $VisualStudioRoot 'VC\Tools\MSVC'
    $msvcVersions = @()
    if (Test-Path -LiteralPath $msvcRoot -PathType Container) {
        $msvcVersions = @(Get-ChildItem -LiteralPath $msvcRoot -Directory -ErrorAction Stop |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+(\.\d+)?$' } |
            Sort-Object { [version]$_.Name } -Descending)
    }
    $selectedMsvc = $null
    $msvcMissing = @()
    foreach ($candidate in $msvcVersions) {
        $required = @(
            'bin\Hostx64\x64\cl.exe', 'bin\Hostx64\x64\link.exe', 'bin\Hostx64\x64\dumpbin.exe',
            'include\vector', 'lib\x64\libcmt.lib', 'lib\x64\libcpmt.lib'
        )
        $missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $candidate.FullName $_) -PathType Leaf) })
        if ($missing.Count -eq 0) { $selectedMsvc = $candidate; break }
        $msvcMissing += "$($candidate.Name): $($missing -join ', ')"
    }
    if (-not $selectedMsvc) {
        throw "No complete MSVC Hostx64/x64 toolset exists under $msvcRoot. Missing: $($msvcMissing -join '; '). Install or repair C++ Build Tools with: $vsInstallCommand"
    }

    $sdkRoots = @()
    foreach ($keyPath in @(
        'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots'
    )) {
        $kitProperties = Get-ItemProperty -LiteralPath $keyPath -Name KitsRoot10 -ErrorAction SilentlyContinue
        $kitRoot = if ($kitProperties) { $kitProperties.KitsRoot10 } else { $null }
        if ($kitRoot -and (Test-Path -LiteralPath $kitRoot -PathType Container)) { $sdkRoots += $kitRoot }
    }
    $defaultSdkRoot = Join-Path $programFilesX86 'Windows Kits\10'
    if (Test-Path -LiteralPath $defaultSdkRoot -PathType Container) { $sdkRoots += $defaultSdkRoot }
    $sdkRoots = @($sdkRoots | Select-Object -Unique)
    if ($SdkVersion -and $SdkVersion -notmatch '^\d+\.\d+\.\d+\.\d+$') {
        throw "Invalid -SdkVersion '$SdkVersion'. Use the full installed version, for example 10.0.22621.0."
    }
    $sdkCandidates = @()
    foreach ($kitRoot in $sdkRoots) {
        $includeRoot = Join-Path $kitRoot 'Include'
        if (-not (Test-Path -LiteralPath $includeRoot -PathType Container)) { continue }
        foreach ($candidate in @(Get-ChildItem -LiteralPath $includeRoot -Directory -ErrorAction Stop)) {
            if ($candidate.Name -notmatch '^\d+\.\d+\.\d+\.\d+$') { continue }
            if ($SdkVersion -and $candidate.Name -ne $SdkVersion) { continue }
            $sdkCandidates += [pscustomobject]@{ Root = $kitRoot; Version = $candidate.Name }
        }
    }
    $selectedSdk = $null
    $sdkMissing = @()
    foreach ($candidate in @($sdkCandidates | Sort-Object { [version]$_.Version } -Descending)) {
        $name = $candidate.Version
        $requiredDirectories = @(
            "Include\$name\ucrt", "Include\$name\shared", "Include\$name\um",
            "Include\$name\winrt", "Include\$name\cppwinrt",
            "Lib\$name\ucrt\x64", "Lib\$name\um\x64"
        )
        $requiredFiles = @(
            "bin\$name\x64\rc.exe", "bin\$name\x64\makeappx.exe",
            "bin\$name\x64\signtool.exe", "bin\$name\x64\makepri.exe",
            "Include\$name\ucrt\stdio.h", "Include\$name\shared\sdkddkver.h",
            "Include\$name\um\Windows.h", "Lib\$name\ucrt\x64\libucrt.lib",
            "Lib\$name\um\x64\kernel32.lib"
        )
        $missing = @($requiredDirectories | Where-Object {
            -not (Test-Path -LiteralPath (Join-Path $candidate.Root $_) -PathType Container)
        })
        $missing += @($requiredFiles | Where-Object {
            -not (Test-Path -LiteralPath (Join-Path $candidate.Root $_) -PathType Leaf)
        })
        if ($missing.Count -eq 0) { $selectedSdk = $candidate; break }
        $sdkMissing += "$name at $($candidate.Root): $($missing -join ', ')"
    }
    if (-not $selectedSdk) {
        $requested = if ($SdkVersion) { " requested version $SdkVersion" } else { '' }
        throw "A complete Windows SDK$requested was not found (x64 headers, libraries, RC, MakeAppx, SignTool and MakePri are required). Missing: $($sdkMissing -join '; '). Install Windows 11 SDK 10.0.22621 with: $sdkInstallCommand . In the SDK installer include Windows SDK for Desktop C++ x64 Apps and Windows SDK Signing Tools."
    }

    $sdkRoot = (Resolve-Path -LiteralPath $selectedSdk.Root -ErrorAction Stop).ProviderPath
    $sdkName = $selectedSdk.Version
    $msvcInclude = Join-Path $selectedMsvc.FullName 'include'
    $msvcLib = Join-Path $selectedMsvc.FullName 'lib\x64'
    $sdkBin = Join-Path $sdkRoot "bin\$sdkName\x64"
    $msvcBin = Join-Path $selectedMsvc.FullName 'bin\Hostx64\x64'
    $msbuild = $null
    foreach ($candidate in @(
        (Join-Path $VisualStudioRoot 'MSBuild\Current\Bin\MSBuild.exe'),
        (Join-Path $VisualStudioRoot 'MSBuild\15.0\Bin\MSBuild.exe')
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $msbuild = $candidate; break }
    }
    $appCert = $null
    foreach ($candidate in @(
        (Join-Path $sdkRoot 'App Certification Kit\appcert.exe'),
        (Join-Path $programFilesX86 'Windows Kits\10\App Certification Kit\appcert.exe'),
        (Join-Path $programFilesX86 'Windows Kits\8.1\App Certification Kit\appcert.exe')
    )) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { $appCert = $candidate; break }
    }
    $dotnet = $null
    $dotnetCommand = Get-Command -Name dotnet.exe -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($dotnetCommand) { $dotnet = $dotnetCommand.Source }
    if (-not $dotnet) {
        $localDotnet = Join-Path (Split-Path -Parent $PSScriptRoot) '.dotnet-sdk\dotnet.exe'
        if (Test-Path -LiteralPath $localDotnet -PathType Leaf) { $dotnet = $localDotnet }
    }
    [pscustomobject]@{
        VisualStudioRoot = $VisualStudioRoot
        MsvcVersion = $selectedMsvc.Name
        Cl = Join-Path $msvcBin 'cl.exe'
        Dumpbin = Join-Path $msvcBin 'dumpbin.exe'
        Link = Join-Path $msvcBin 'link.exe'
        Rc = Join-Path $sdkBin 'rc.exe'
        MakeAppx = Join-Path $sdkBin 'makeappx.exe'
        SignTool = Join-Path $sdkBin 'signtool.exe'
        MakePri = Join-Path $sdkBin 'makepri.exe'
        MSBuild = $msbuild
        SdkRoot = $sdkRoot
        SdkVersion = $sdkName
        MsvcInclude = $msvcInclude
        MsvcLib = $msvcLib
        IncludePaths = @(
            $msvcInclude, (Join-Path $sdkRoot "Include\$sdkName\ucrt"),
            (Join-Path $sdkRoot "Include\$sdkName\shared"), (Join-Path $sdkRoot "Include\$sdkName\um"),
            (Join-Path $sdkRoot "Include\$sdkName\winrt"), (Join-Path $sdkRoot "Include\$sdkName\cppwinrt")
        )
        LibraryPaths = @(
            $msvcLib, (Join-Path $sdkRoot "Lib\$sdkName\ucrt\x64"),
            (Join-Path $sdkRoot "Lib\$sdkName\um\x64")
        )
        AppCert = $appCert
        DotNet = $dotnet
    }
}

function Invoke-WindowCueTool {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][ValidateNotNullOrEmpty()][string]$Path,
        [string[]]$Arguments = @(),
        [string]$LogPath
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required command-line tool does not exist: $Path"
    }
    if ($LogPath) {
        $logDirectory = Split-Path -Parent $LogPath
        if ($logDirectory -and -not (Test-Path -LiteralPath $logDirectory -PathType Container)) {
            New-Item -ItemType Directory -Path $logDirectory -Force -ErrorAction Stop | Out-Null
        }
    }
    # PowerShell 5.1 represents redirected native stderr as ErrorRecord objects.
    # Keep that output in the stream, then determine failure from the native exit code.
    $ErrorActionPreference = 'Continue'
    # MakePri emits UTF-16LE even when invoked by Windows PowerShell's native pipeline.
    $previousEncoding = [Console]::OutputEncoding
    try {
        if ([IO.Path]::GetFileName($Path) -ieq 'makepri.exe') { [Console]::OutputEncoding = [Text.Encoding]::Unicode }
        & $Path @Arguments 2>&1 | ForEach-Object {
            $line = $_.ToString()
            Write-Host $line
            if ($LogPath) { Add-Content -LiteralPath $LogPath -Value $line -Encoding UTF8 -ErrorAction Stop }
        }
        $toolExitCode = $LASTEXITCODE
    } finally { [Console]::OutputEncoding = $previousEncoding }
    if ($toolExitCode -ne 0) {
        $logHint = if ($LogPath) { " See log: $LogPath" } else { '' }
        throw "Tool '$Path' failed with exit code $toolExitCode.$logHint"
    }
}
