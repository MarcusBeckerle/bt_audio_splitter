[CmdletBinding()]
param(
    [string]$Repository = 'MarcusBeckerle/bt_audio_splitter',
    [string]$GitHubCli = 'gh'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$publicFile = Join-Path $root 'signing/Marcus-Beckerle.cer'
$friendlyName = 'BT Audio Splitter release signing (self-signed)'
$environmentName = 'release-signing'

function Invoke-GitHub([string[]]$Arguments, [string]$InputText = '') {
    if ($InputText) { $result = $InputText | & $GitHubCli @Arguments }
    else { $result = & $GitHubCli @Arguments }
    if ($LASTEXITCODE -ne 0) { throw 'GitHub configuration failed; no secret values are included in this error.' }
    return $result
}

# Reuse an existing identity. Never silently rotate a published signing certificate.
if (Test-Path -LiteralPath $publicFile) {
    $public = [Security.Cryptography.X509Certificates.X509Certificate2]::new($publicFile)
    $certificate = Get-Item -LiteralPath ("Cert:\CurrentUser\My\" + $public.Thumbprint) -ErrorAction SilentlyContinue
    if (-not $certificate -or -not $certificate.HasPrivateKey) { throw 'The published certificate private key is not available in CurrentUser/My. Restore its encrypted backup before provisioning.' }
} else {
    $certificate = @(Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Where-Object { $_.FriendlyName -eq $friendlyName -and $_.Subject -eq 'CN=Marcus Beckerle' -and $_.HasPrivateKey }) | Select-Object -First 1
    if (-not $certificate) {
        $certificate = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=Marcus Beckerle' -FriendlyName $friendlyName -CertStoreLocation Cert:\CurrentUser\My -KeyAlgorithm RSA -KeyLength 3072 -HashAlgorithm SHA256 -KeyUsage DigitalSignature -KeyExportPolicy Exportable -NotAfter (Get-Date).AddYears(3)
    }
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $publicFile) | Out-Null
    Export-Certificate -Cert $certificate -FilePath $publicFile -Type CERT | Out-Null
}
if ($certificate.NotAfter -le (Get-Date).AddDays(30)) { throw 'The signing certificate is expired or expires within 30 days.' }

# The local certificate stays in My, never Root/TrustedPublisher. Only its public half is committed.
$environments = Invoke-GitHub @('api', "repos/$Repository/environments", '--jq', '.environments[].name')
if ($environments -notcontains $environmentName) {
    $settings = @{ deployment_branch_policy = @{ protected_branches = $false; custom_branch_policies = $true } } | ConvertTo-Json -Compress
    Invoke-GitHub @('api', '--method', 'PUT', "repos/$Repository/environments/$environmentName", '--input', '-') $settings | Out-Null
    foreach ($rule in @(@{ name = 'v*'; type = 'tag' }, @{ name = 'main'; type = 'branch' })) {
        Invoke-GitHub @('api', '--method', 'POST', "repos/$Repository/environments/$environmentName/deployment-branch-policies", '--input', '-') ($rule | ConvertTo-Json -Compress) | Out-Null
    }
}

$random = [Security.Cryptography.RandomNumberGenerator]::Create()
$bytes = New-Object byte[] 48
$random.GetBytes($bytes); $random.Dispose()
$password = [Convert]::ToBase64String($bytes)
$pfx = $null; $encoded = $null
try {
    # No PFX or plaintext password is written to disk. gh encrypts secrets using GitHub's public key.
    $pfx = $certificate.Export([Security.Cryptography.X509Certificates.X509ContentType]::Pfx, $password)
    $encoded = [Convert]::ToBase64String($pfx)
    Invoke-GitHub @('secret', 'set', 'WINDOWS_SIGNING_PFX', '--repo', $Repository, '--env', $environmentName) $encoded | Out-Null
    Invoke-GitHub @('secret', 'set', 'WINDOWS_SIGNING_PASSWORD', '--repo', $Repository, '--env', $environmentName) $password | Out-Null
} finally {
    if ($pfx) { [Array]::Clear($pfx, 0, $pfx.Length) }
    [Array]::Clear($bytes, 0, $bytes.Length)
    $password = $null; $encoded = $null
}
$certificate | Select-Object Subject, Thumbprint, NotAfter
Write-Host 'Public certificate exported; signing secrets configured in the release-signing environment.'
