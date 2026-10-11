$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$dotnet = Join-Path $projectRoot '.dotnet-sdk\dotnet.exe'
$project = Join-Path $PSScriptRoot 'WindowCue.csproj'
$output = Join-Path $projectRoot 'dist\WindowCue'
$stageRoot = Join-Path $PSScriptRoot 'artifacts\publish'
$env:DOTNET_CLI_HOME = Join-Path $projectRoot '.dotnet-cli-home'
$env:DOTNET_NOLOGO = '1'
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'

if (-not (Test-Path -LiteralPath $dotnet)) {
    throw "Local .NET SDK not found: $dotnet"
}

function Publish-WindowCue {
    param(
        [Parameter(Mandatory = $true)][string]$Suffix,
        [Parameter(Mandatory = $true)][string]$LanguageConstant
    )

    $assemblyName = "WindowCue_$Suffix"
    $stageOutput = Join-Path $stageRoot $Suffix
    & $dotnet publish $project `
        -c Release `
        -r win-x64 `
        --self-contained false `
        -p:PublishSingleFile=true `
        -p:DebugType=None `
        -p:DebugSymbols=false `
        "-p:AssemblyName=$assemblyName" `
        "-p:DefineConstants=$LanguageConstant" `
        -o $stageOutput

    if ($LASTEXITCODE -ne 0) {
        throw "Publish $Suffix failed with exit code: $LASTEXITCODE"
    }

    New-Item -ItemType Directory -Force -Path $output | Out-Null
    Copy-Item -LiteralPath (Join-Path $stageOutput "$assemblyName.exe") `
              -Destination (Join-Path $output "$assemblyName.exe") `
              -Force
}

Publish-WindowCue -Suffix 'CN' -LanguageConstant 'LANG_CN'
Publish-WindowCue -Suffix 'EN' -LanguageConstant 'LANG_EN'

Write-Host "Published: $output\WindowCue_CN.exe"
Write-Host "Published: $output\WindowCue_EN.exe"
