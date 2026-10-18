[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$Bootstrap,
    [string]$Version = ''
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if (-not $Version) { $Version = [IO.File]::ReadAllText((Join-Path $root 'VERSION')).Trim() }
if ($Version -notmatch '^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?$') { throw 'Version must be a numeric major.minor.patch, optionally followed by a prerelease suffix.' }
$parts = @($Matches[1], $Matches[2], $Matches[3])
foreach ($part in $parts) { if ([long]$part -gt 65535) { throw 'Windows file-version components must be <= 65535.' } }
$toolchain = Join-Path $root '.tools/llvm-mingw-20261006-ucrt-x86_64'
$compiler = Join-Path $toolchain 'bin/clang++.exe'
# Build tools stay local. End users only need the resulting EXE.
if (-not (Test-Path -LiteralPath $compiler)) {
    if (-not $Bootstrap) { throw 'Run ./build.ps1 -Bootstrap to download the pinned portable C++ toolchain.' }
    New-Item -ItemType Directory -Force -Path (Join-Path $root '.tools') | Out-Null
    $archive = Join-Path $root '.tools/llvm-mingw.zip'
    $sha256 = '317492c456aa27ee607a5919f1d2d38dcdc1112516a24d0bf4b00d078f52d17a'
    if (-not (Test-Path -LiteralPath $archive) -or (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $sha256) {
        Write-Host 'Downloading official LLVM-MinGW 20261006 into .tools ...'
        Invoke-WebRequest 'https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/llvm-mingw-20261006-ucrt-x86_64.zip' -OutFile $archive -UseBasicParsing
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $sha256) { throw 'Compiler checksum mismatch.' }
    Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $root '.tools') -Force
}
function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
Push-Location $root
try {
    $out = Join-Path $root 'artifacts/native'
    $release = Join-Path $root 'artifacts/release'
    $package = Join-Path $out "BT-Audio-Splitter-$version-win-x64"
    # Only replace our exact generated package directory, never arbitrary paths.
    $resolvedPackage = [IO.Path]::GetFullPath($package)
    if (-not $resolvedPackage.StartsWith([IO.Path]::GetFullPath($out) + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe package directory.' }
    if (Test-Path -LiteralPath $resolvedPackage) { Remove-Item -LiteralPath $resolvedPackage -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $out, $release, $package | Out-Null
    $versionHeader = '#define APP_VERSION_TEXT "' + $Version + '"' + "`n" + '#define APP_VERSION_NUMERIC ' + ($parts -join ',') + ',0' + "`n"
    [IO.File]::WriteAllText((Join-Path $out 'version.h'), $versionHeader, [Text.UTF8Encoding]::new($false))
    $documents = @('LICENSE', 'NOTICE', 'LICENSING.md', 'COMMERCIAL-LICENSE.md', 'THIRD-PARTY-NOTICES.md')
    $licenses = @(Get-ChildItem (Join-Path $root 'licenses') -File | Sort-Object Name)
    $text = (($documents + @($licenses.FullName)) | ForEach-Object { [IO.File]::ReadAllText((Resolve-Path -LiteralPath $_)) + "`r`n`r`n" }) -join ''
    [IO.File]::WriteAllText((Join-Path $out 'licenses.txt'), $text, [Text.UTF8Encoding]::new($false))
    Invoke-Checked (Join-Path $toolchain 'bin/x86_64-w64-mingw32-windres.exe') @('-I', 'native', 'native/app.rc', "$out/app.res")
    $flags = @('-std=c++20', '-D_WIN32_WINNT=0x0A00', '-Wall', '-Wextra', '-Werror', '-static', '-fstack-protector-strong', '-Wl,--nxcompat,--dynamicbase,--high-entropy-va')
    if ($Configuration -eq 'Release') { $flags += @('-O2', '-s', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections') }
    else { $flags += @('-O0', '-g') }
    Invoke-Checked $compiler ($flags + @('native/tests/tests.cpp', 'native/platform.cpp', '-o', "$out/tests.exe", '-lole32', '-luuid', '-lshell32'))
    Invoke-Checked "$out/tests.exe" @()
    Invoke-Checked $compiler ($flags + @('-municode', '-mwindows', 'native/main.cpp', 'native/platform.cpp', 'native/audio.cpp', 'native/setup.cpp', 'native/ui.cpp', "$out/app.res", '-o', "$out/AudioSplitter.exe", '-lole32', '-luuid', '-lpropsys', '-lksuser', '-lcomctl32', '-lgdiplus', '-lshell32', '-lwinhttp', '-lbcrypt', '-lwintrust', '-lavrt'))
    if ($Configuration -eq 'Release' -and (Get-Item -LiteralPath "$out/AudioSplitter.exe").Length -gt 3000000) { throw 'Unexpected executable size regression (> 3 MB).' }
    # Fail packaging if a compiler runtime DLL slipped into the standalone binary.
    $imports = & (Join-Path $toolchain 'bin/llvm-readobj.exe') --coff-imports "$out/AudioSplitter.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect executable dependencies.' }
    if (($imports -join "`n") -match '(?i)(libc\+\+|libunwind|libwinpthread|vcruntime|msvcp|libgcc).*\.dll') { throw 'Unexpected external runtime dependency.' }
    $imports | Set-Content -LiteralPath "$out/imports.txt" -Encoding UTF8
    Copy-Item -LiteralPath "$out/AudioSplitter.exe" -Destination "$package/AudioSplitter.exe" -Force
    foreach ($file in ($documents + @('README.md', 'CHANGELOG.md'))) { Copy-Item -LiteralPath $file -Destination $package -Force }
    Copy-Item -LiteralPath 'licenses' -Destination $package -Recurse -Force
    Copy-Item -LiteralPath 'signing' -Destination $package -Recurse -Force
    $name = "BT-Audio-Splitter-$version-win-x64"
    Copy-Item -LiteralPath "$out/AudioSplitter.exe" -Destination "$release/$name.exe" -Force
    Compress-Archive -Path "$package/*" -DestinationPath "$release/$name.zip" -Force
    $hashes = @("$name.exe", "$name.zip") | ForEach-Object { '{0}  {1}' -f (Get-FileHash -LiteralPath "$release/$_" -Algorithm SHA256).Hash.ToLowerInvariant(), $_ }
    [IO.File]::WriteAllLines("$release/SHA256SUMS.txt", $hashes, [Text.UTF8Encoding]::new($false))
    Get-Item -LiteralPath "$release/$name.exe", "$release/$name.zip" | Select-Object Name, Length
} finally { Pop-Location }
