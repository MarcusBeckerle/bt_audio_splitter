[CmdletBinding()]
param([Parameter(Mandatory)][string]$Version)
$ErrorActionPreference = 'Stop'
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_ENVIRONMENT -ne 'github-hosted') { throw 'Signing and temporary trust verification run only on an ephemeral GitHub-hosted runner.' }
if ($Version -notmatch '^\d+\.\d+\.\d+(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?$') { throw 'Invalid release version.' }
if (-not $env:WINDOWS_SIGNING_PFX -or -not $env:WINDOWS_SIGNING_PASSWORD) { throw 'Release signing secrets are missing.' }
$root = Split-Path -Parent $PSScriptRoot
$release = Join-Path $root 'artifacts/release'
$name = "BT-Audio-Splitter-$Version-win-x64"
$exe = Join-Path $release "$name.exe"
$zip = Join-Path $release "$name.zip"
$publicFile = Join-Path $root 'signing/Marcus-Beckerle.cer'
$expected = [Security.Cryptography.X509Certificates.X509Certificate2]::new($publicFile)
$thumbprint = $expected.Thumbprint
$myPath = "Cert:\CurrentUser\My\$thumbprint"
$trustPath = "Cert:\LocalMachine\Root\$thumbprint"
if (Test-Path -LiteralPath $myPath) { throw 'Runner already contains the signing identity; refusing ambiguous cleanup.' }
if (Test-Path -LiteralPath $trustPath) { throw 'Runner already trusts this certificate; refusing ambiguous cleanup.' }
$temporary = Join-Path $env:RUNNER_TEMP ('audio-splitter-signing-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary | Out-Null
$pfxPath = Join-Path $temporary 'signing.pfx'
try {
    Write-Host 'Importing the release identity into the disposable runner.'
    $pfxBytes = [Convert]::FromBase64String($env:WINDOWS_SIGNING_PFX)
    [IO.File]::WriteAllBytes($pfxPath, $pfxBytes); [Array]::Clear($pfxBytes, 0, $pfxBytes.Length)
    $password = ConvertTo-SecureString $env:WINDOWS_SIGNING_PASSWORD -AsPlainText -Force
    $imported = @(Import-PfxCertificate -FilePath $pfxPath -CertStoreLocation Cert:\CurrentUser\My -Password $password)
    $certificate = $imported | Where-Object { $_.Thumbprint -eq $thumbprint -and $_.HasPrivateKey } | Select-Object -First 1
    if (-not $certificate -or $certificate.Subject -ne 'CN=Marcus Beckerle' -or $certificate.NotAfter -le (Get-Date)) { throw 'Signing key does not match the committed, valid Marcus Beckerle certificate.' }
    $purposes = @($certificate.EnhancedKeyUsageList | ForEach-Object { $_.ObjectId.ToString() })
    if ($purposes -notcontains '1.3.6.1.5.5.7.3.3') { throw 'Certificate is not approved for code signing.' }
    $env:WINDOWS_SIGNING_PFX = $null; $env:WINDOWS_SIGNING_PASSWORD = $null

    $sdk = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/bin'
    $signTool = Get-ChildItem -Path "$sdk/*/x64/signtool.exe" | Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $signTool) { throw 'Windows SDK SignTool is missing.' }
    Write-Host 'Signing and requesting the RFC 3161 timestamp.'
    & $signTool.FullName sign /sha1 $thumbprint /s My /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $exe
    if ($LASTEXITCODE -ne 0) { throw 'Authenticode signing or RFC 3161 timestamping failed.' }

    # Trust only this exact self-signed identity, only on the disposable runner, for verification.
    # No trust-store changes are performed on a developer's or end user's computer.
    # CurrentUser/Root can open an interactive trust dialog and hang unattended CI.
    # The elevated hosted runner uses its machine store; this exact entry is removed below.
    Write-Host 'Temporarily trusting the pinned public certificate on the disposable runner.'
    Import-Certificate -FilePath $publicFile -CertStoreLocation Cert:\LocalMachine\Root | Out-Null
    Write-Host 'Verifying the signature, publisher and timestamp.'
    & $signTool.FullName verify /pa /all /v $exe
    if ($LASTEXITCODE -ne 0) { throw 'Authenticode verification failed.' }
    $signature = Get-AuthenticodeSignature -LiteralPath $exe
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Thumbprint -ne $thumbprint -or -not $signature.TimeStamperCertificate) { throw 'Signature identity, integrity or timestamp validation failed.' }

    Write-Host 'Packaging the verified signed executable.'
    $package = Join-Path $temporary 'package'
    Expand-Archive -LiteralPath $zip -DestinationPath $package
    Copy-Item -LiteralPath $exe -Destination (Join-Path $package 'AudioSplitter.exe') -Force
    $signingDocs = Join-Path $package 'signing'
    New-Item -ItemType Directory -Path $signingDocs -Force | Out-Null
    Copy-Item -LiteralPath $publicFile -Destination $signingDocs
    Copy-Item -LiteralPath (Join-Path $root 'signing/README.md') -Destination (Join-Path $signingDocs 'README.md')
    Compress-Archive -Path "$package/*" -DestinationPath $zip -Force
    Copy-Item -LiteralPath $publicFile -Destination $release -Force
    $files = @("$name.exe", "$name.zip", 'Marcus-Beckerle.cer')
    $hashes = foreach ($file in $files) { '{0}  {1}' -f (Get-FileHash -LiteralPath (Join-Path $release $file) -Algorithm SHA256).Hash.ToLowerInvariant(), $file }
    [IO.File]::WriteAllLines((Join-Path $release 'SHA256SUMS.txt'), $hashes, [Text.UTF8Encoding]::new($false))
    # Verify that the ZIP ships exactly the signed executable, not the unsigned build.
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($zip)
    try {
        $entry = $archive.GetEntry('AudioSplitter.exe')
        if (-not $entry) { throw 'ZIP lacks the signed executable.' }
        $stream = $entry.Open(); $sha = [Security.Cryptography.SHA256]::Create()
        try { $zipHash = [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
        finally { $stream.Dispose(); $sha.Dispose() }
        if ($zipHash -ne (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash) { throw 'EXE and ZIP payload differ.' }
    } finally { $archive.Dispose() }
    Write-Host "Verified signed release $Version; publisher: Marcus Beckerle; certificate: $thumbprint"
} finally {
    $env:WINDOWS_SIGNING_PFX = $null; $env:WINDOWS_SIGNING_PASSWORD = $null
    if (Test-Path -LiteralPath $trustPath) { Remove-Item -LiteralPath $trustPath -Force }
    if (Test-Path -LiteralPath $myPath) { Remove-Item -LiteralPath $myPath -DeleteKey -Force }
    $resolved = [IO.Path]::GetFullPath($temporary)
    if (-not $resolved.StartsWith([IO.Path]::GetFullPath($env:RUNNER_TEMP) + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe signing cleanup directory.' }
    if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
}
