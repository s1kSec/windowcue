[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackagePath,
    [string]$CertificatePath,
    [switch]$TrustCertificate
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Tools.ps1')
$tools = Get-WindowCueToolchain
$PackagePath = (Resolve-Path -LiteralPath $PackagePath).ProviderPath
$signature = Get-AuthenticodeSignature -LiteralPath $PackagePath
if (-not $signature.SignerCertificate) { throw 'Package is unsigned. Run Sign-Local.ps1 first.' }
if ($signature.Status -eq 'HashMismatch' -or $signature.Status -eq 'NotSigned') {
    throw 'Package signature is damaged. No certificate trust or package installation was performed.'
}
if ($TrustCertificate) {
    if (-not $CertificatePath) { throw '-CertificatePath is required with -TrustCertificate.' }
    $CertificatePath = (Resolve-Path -LiteralPath $CertificatePath).ProviderPath
    $certificate = [Security.Cryptography.X509Certificates.X509Certificate2]::new($CertificatePath)
    if ($certificate.Thumbprint -ne $signature.SignerCertificate.Thumbprint) { throw 'Supplied certificate differs from package signer.' }
    $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Certificate trust in LocalMachine\TrustedPeople requires an elevated PowerShell. No settings were changed.'
    }
    Import-Certificate -FilePath $CertificatePath -CertStoreLocation 'Cert:\LocalMachine\TrustedPeople' | Out-Null
}
# Check both trust and signed payload integrity before installing.
Invoke-WindowCueTool -Path $tools.SignTool -Arguments @('verify', '/pa', '/v', $PackagePath)
Add-AppxPackage -Path $PackagePath
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($PackagePath)
try {
    $reader = [IO.StreamReader]::new($zip.GetEntry('AppxManifest.xml').Open())
    try { [xml]$manifest = $reader.ReadToEnd() } finally { $reader.Dispose() }
} finally { $zip.Dispose() }
$installed = Get-AppxPackage -Name $manifest.Package.Identity.Name |
    Where-Object { $_.Publisher -eq $manifest.Package.Identity.Publisher }
if (-not $installed) { throw 'Installed package could not be verified for this Windows user.' }
$installed | Select-Object Name, Version, Architecture, PackageFamilyName, InstallLocation
Write-Host "Start menu activation: explorer.exe shell:AppsFolder\$($installed.PackageFamilyName)!WindowCue"
Write-Host 'Use an isolated Windows test user for first launch: existing WindowCue settings and Alt+P may be affected.'
