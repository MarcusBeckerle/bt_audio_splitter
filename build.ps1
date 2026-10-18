[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [ValidatePattern('^\d+\.\d+\.\d+(-[A-Za-z0-9.]+)?$')][string]$Version = '0.1.0',
    [switch]$Bootstrap
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$toolsDir = Join-Path $root '.tools'
$localDotnet = Join-Path $toolsDir 'dotnet\dotnet.exe'

# Optional local SDK bootstrap: never installs tools globally or on an end user's PC.
if ($Bootstrap -and -not (Test-Path -LiteralPath $localDotnet)) {
    New-Item -ItemType Directory -Force -Path $toolsDir | Out-Null
    $archive = Join-Path $toolsDir 'dotnet-sdk.zip'
    $uri = 'https://builds.dotnet.microsoft.com/dotnet/Sdk/10.0.402/dotnet-sdk-10.0.402-win-x64.zip'
    $sha512 = 'ae4fd528cb7e9f6af6aaf7de36715a69f761f183744f1bb3025d19f5a92447090ec561410e46266c1d10a63781ed6f3de771760bc76c876d87daabc367b898cd'
    Write-Host 'Downloading the official .NET SDK into .tools ...'
    Invoke-WebRequest -Uri $uri -OutFile $archive -UseBasicParsing
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA512).Hash -ne $sha512) { throw 'SDK checksum mismatch.' }
    Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $toolsDir 'dotnet') -Force
}

if (Test-Path -LiteralPath $localDotnet) { $dotnet = $localDotnet }
else {
    $command = Get-Command dotnet -ErrorAction SilentlyContinue
    if ($null -eq $command) { throw 'Install the .NET 10 SDK, or run build.ps1 -Bootstrap.' }
    $dotnet = $command.Source
}

function Invoke-Dotnet {
    & $dotnet @args
    if ($LASTEXITCODE -ne 0) { throw "dotnet failed with exit code $LASTEXITCODE" }
}

Push-Location $root
try {
    $env:DOTNET_CLI_HOME = Join-Path $toolsDir 'cli'
    $env:NUGET_PACKAGES = Join-Path $toolsDir 'nuget'
    $env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
    $env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'

    $project = 'src\BtAudioSplitter\BtAudioSplitter.csproj'
    $tests = 'tests\BtAudioSplitter.Tests\BtAudioSplitter.Tests.csproj'
    Invoke-Dotnet restore $project -r win-x64 --locked-mode
    Invoke-Dotnet restore $tests --locked-mode
    Invoke-Dotnet run --project $tests -c $Configuration --no-restore '-p:UseSharedCompilation=false'

    $publish = Join-Path $root "artifacts\publish\$Version\win-x64"
    Invoke-Dotnet publish $project -c $Configuration -r win-x64 --self-contained true --no-restore `
        "-p:Version=$Version" '-p:UseSharedCompilation=false' -o $publish

    # Symbol files are useful locally, but are not needed in the portable user package.
    Get-ChildItem -LiteralPath $publish -Filter '*.pdb' -File | ForEach-Object {
        Remove-Item -LiteralPath $_.FullName
    }

    # Notices are embedded in the EXE, and also included as readable files in the ZIP.
    foreach ($file in @('README.md', 'LICENSE', 'NOTICE', 'LICENSING.md', 'COMMERCIAL-LICENSE.md', 'THIRD-PARTY-NOTICES.md', 'CHANGELOG.md')) {
        Copy-Item -LiteralPath (Join-Path $root $file) -Destination $publish -Force
    }
    Copy-Item -LiteralPath (Join-Path $root 'licenses') -Destination $publish -Recurse -Force

    $release = Join-Path $root 'artifacts\release'
    New-Item -ItemType Directory -Force -Path $release | Out-Null
    $name = "BT-Audio-Splitter-$Version-win-x64"
    $zip = Join-Path $release "$name.zip"
    $exe = Join-Path $release "$name.exe"
    Copy-Item -LiteralPath (Join-Path $publish 'BT Audio Splitter.exe') -Destination $exe -Force
    Compress-Archive -Path (Join-Path $publish '*') -DestinationPath $zip -Force
    $hashes = foreach ($file in @($exe, $zip)) {
        '{0}  {1}' -f (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant(), [IO.Path]::GetFileName($file)
    }
    $hashes | Set-Content -LiteralPath (Join-Path $release "SHA256SUMS-$Version.txt") -Encoding ascii
    Write-Host "Release ready: $release"
}
finally { Pop-Location }
