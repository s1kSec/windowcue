[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackagePath,
    [switch]$CreateCertificate,
    [string]$CertificateThumbprint
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Tools.ps1')
$tools = Get-WindowCueToolchain
$PackagePath = (Resolve-Path -LiteralPath $PackagePath).ProviderPath
if ([IO.Path]::GetExtension($PackagePath) -ine '.msix') { throw 'A .msix package is required.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($PackagePath)
try {
    $entry = $zip.GetEntry('AppxManifest.xml')
    if (-not $entry) { throw 'Package does not contain AppxManifest.xml.' }
    if ($zip.GetEntry('AppxSignature.p7x')) { throw 'Input must be the unsigned package; preserve signed packages separately.' }
    $reader = [IO.StreamReader]::new($entry.Open(), [Text.Encoding]::UTF8)
    try { [xml]$manifest = $reader.ReadToEnd() } finally { $reader.Dispose() }
} finally { $zip.Dispose() }
$publisher = [string]$manifest.Package.Identity.Publisher
$friendlyName = "WindowCue MSIX Local Test ($publisher)"
if ($CertificateThumbprint) {
    $certificate = Get-Item -LiteralPath "Cert:\CurrentUser\My\$CertificateThumbprint"
} else {
    $certificate = Get-ChildItem -LiteralPath 'Cert:\CurrentUser\My' |
        Where-Object { $_.FriendlyName -eq $friendlyName -and $_.Subject -ceq $publisher -and
            $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date).AddDays(1) } |
        Sort-Object NotAfter -Descending | Select-Object -First 1
    if (-not $certificate) {
        if (-not $CreateCertificate) { throw 'No matching WindowCue test certificate. Specify -CreateCertificate or -CertificateThumbprint.' }
        $certificate = New-SelfSignedCertificate -Type Custom -Subject $publisher -FriendlyName $friendlyName `
            -CertStoreLocation 'Cert:\CurrentUser\My' -KeyAlgorithm RSA -KeyLength 2048 `
            -HashAlgorithm SHA256 -KeyUsage DigitalSignature -KeyExportPolicy NonExportable `
            -NotAfter (Get-Date).AddMonths(6) `
            -TextExtension @('2.5.29.37={text}1.3.6.1.5.5.7.3.3', '2.5.29.19={text}')
    }
}
if ($certificate.Subject -cne $publisher) { throw 'Certificate Subject does not exactly match manifest Publisher.' }
if (-not $certificate.HasPrivateKey -or $certificate.NotAfter -le (Get-Date)) { throw 'Certificate private key missing or expired.' }
if ('1.3.6.1.5.5.7.3.3' -notin @($certificate.EnhancedKeyUsageList | ForEach-Object { [string]$_.ObjectId })) {
    throw 'Certificate must have the code-signing EKU.'
}
$certRoot = Join-Path $PSScriptRoot 'certificates'
New-Item -ItemType Directory -Force -Path $certRoot | Out-Null
$certificatePath = Join-Path $certRoot ($certificate.Thumbprint + '.cer')
Export-Certificate -Cert $certificate -FilePath $certificatePath -Type CERT | Out-Null
$signedPath = $PackagePath.Substring(0, $PackagePath.Length - 5) + '.local-signed.msix'
if (Test-Path -LiteralPath $signedPath) { throw "Signed output already exists: $signedPath. Rebuild or use a new package filename; existing signatures are preserved." }
Copy-Item -LiteralPath $PackagePath -Destination $signedPath
$logPath = $signedPath + '.sign.log'
Invoke-WindowCueTool -Path $tools.SignTool -Arguments @('sign', '/fd', 'SHA256', '/sha1', $certificate.Thumbprint, '/s', 'My', $signedPath) -LogPath $logPath
$signature = Get-AuthenticodeSignature -LiteralPath $signedPath
if (-not $signature.SignerCertificate -or $signature.SignerCertificate.Thumbprint -ne $certificate.Thumbprint) {
    throw 'Signed package does not expose the expected signing certificate.'
}
if ($signature.Status -eq 'HashMismatch') { throw 'Signed package hash validation failed.' }
$report = [ordered]@{
    PackagePath = $signedPath; UnsignedPackagePath = $PackagePath; Publisher = $publisher
    CertificatePath = $certificatePath; CertificateThumbprint = $certificate.Thumbprint
    CertificateSubject = $certificate.Subject; CertificateStore = 'CurrentUser\My'; TrustedByThisScript = $false
    SignatureStatus = [string]$signature.Status; SignatureStatusMessage = $signature.StatusMessage
    SignToolSigning = 'Passed'; SHA256 = (Get-FileHash -LiteralPath $signedPath -Algorithm SHA256).Hash
    Purpose = 'Local test only; not a production signing certificate'
}
$report | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath ($signedPath + '.sign-report.json') -Encoding UTF8
Write-Host "Signed test package: $signedPath"
Write-Host "Public test certificate: $certificatePath"
Write-Host 'No certificate trust or package installation was performed.'
[pscustomobject]$report
