[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$PackagePath)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Tools.ps1')
$tools = Get-WindowCueToolchain
if (-not $tools.AppCert) { throw 'Windows App Certification Kit is missing. Install that component from the official Windows SDK installer.' }
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'WACK requires elevated PowerShell in an active, isolated Windows test user session.'
}
$PackagePath = (Resolve-Path -LiteralPath $PackagePath).ProviderPath
Invoke-WindowCueTool -Path $tools.SignTool -Arguments @('verify', '/pa', $PackagePath)
$reportRoot = Join-Path $PSScriptRoot ('artifacts\wack-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $reportRoot -Force | Out-Null
$reportPath = Join-Path $reportRoot 'report.xml'
Write-Host 'WACK may install, activate and close the package. Run it in an isolated Windows test account.'
Invoke-WindowCueTool -Path $tools.AppCert -Arguments @('reset') -LogPath (Join-Path $reportRoot 'reset.log')
Invoke-WindowCueTool -Path $tools.AppCert -Arguments @('test', '-appxpackagepath', $PackagePath, '-reportoutputpath', $reportPath) -LogPath (Join-Path $reportRoot 'test.log')
if (-not (Test-Path -LiteralPath $reportPath)) { throw 'WACK did not produce a report.' }
Write-Host "WACK report: $reportPath. Review individual test results before submitting."
Get-Item -LiteralPath $reportPath
