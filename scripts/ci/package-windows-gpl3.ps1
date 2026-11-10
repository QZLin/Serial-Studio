# SPDX-FileCopyrightText: 2026 QZLin
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs on the GitHub Actions Windows runner after cmake --install.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:GITHUB_ACTIONS -ne 'true') {
    throw 'This packaging script must run on a GitHub Actions runner, not locally.'
}
if ($env:EXECUTABLE -ne 'Serial-Studio-GPL3') {
    throw 'This script only packages the GPLv3 edition.'
}

$root = Join-Path $env:GITHUB_WORKSPACE "dist/$env:EXECUTABLE"
$bin = Join-Path $root 'bin'
$cache = Get-Content 'build/CMakeCache.txt' -Raw
if ($cache -notmatch '(?m)^BUILD_GPL3:BOOL=ON\r?$' -or
    $cache -notmatch '(?m)^BUILD_COMMERCIAL:BOOL=OFF\r?$') {
    throw 'Refusing to package a build that is not explicitly GPLv3.'
}

$config = Get-Content 'build/CPackConfig.cmake' -Raw
$versionMatch = [regex]::Match($config, 'set\(CPACK_PACKAGE_VERSION "([0-9.]+)"\)')
if (-not $versionMatch.Success) { throw 'Cannot read the configured application version.' }
$version = $versionMatch.Groups[1].Value
$shortSha = $env:GITHUB_SHA.Substring(0, 8)
$name = "$env:EXECUTABLE-$version-Windows-x64-Portable-$shortSha"

# Qt's deploy script installs the normal Windows plugin only. Include offscreen
# too so the very same downloaded ZIP can run its in-app QML self-test.
$qtBin = Split-Path (Get-Command windeployqt.exe -ErrorAction Stop).Source -Parent
$qtRoot = Split-Path $qtBin -Parent
$platforms = Join-Path $root 'plugins/platforms'
New-Item -ItemType Directory -Force $platforms | Out-Null
Copy-Item (Join-Path $qtRoot 'plugins/platforms/qoffscreen.dll') $platforms

$required = @(
    "bin/$env:EXECUTABLE.exe",
    'bin/Qt6Core.dll',
    'bin/Qt6Gui.dll',
    'bin/Qt6Qml.dll',
    'bin/Qt6Quick.dll',
    'bin/Qt6Graphs.dll',
    'bin/Qt6SerialPort.dll',
    'bin/Qt6WebEngineQuick.dll',
    'bin/msvcp140.dll',
    'bin/vcruntime140.dll',
    'bin/qt.conf',
    'bin/ss-config.json',
    'plugins/platforms/qwindows.dll',
    'plugins/platforms/qoffscreen.dll',
    'plugins/tls/qschannelbackend.dll',
    'qml/QtQuick/Controls/qmldir'
)
foreach ($relative in $required) {
    if (-not (Test-Path (Join-Path $root $relative) -PathType Leaf)) {
        throw "Portable dependency missing: $relative"
    }
}
$stamp = Get-Content (Join-Path $bin 'ss-config.json') -Raw | ConvertFrom-Json
if ($stamp.packageType -ne 'portable' -or $stamp.arch -ne 'x86_64') {
    throw 'The package-type / architecture stamp is wrong.'
}

Copy-Item -Recurse 'LICENSES' (Join-Path $root 'LICENSES')
Copy-Item 'LICENSES/GPL-3.0-or-later.txt' (Join-Path $root 'LICENSE.txt')
Copy-Item 'app/rcc/messages/Acknowledgements.txt' (Join-Path $root 'Acknowledgements.txt')

$sourceUrl = "https://github.com/$env:GITHUB_REPOSITORY/archive/$env:GITHUB_SHA.zip"
$runUrl = "https://github.com/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID"
[ordered]@{
    application = 'Serial Studio GPLv3'
    version = $version
    commit = $env:GITHUB_SHA
    repository = $env:GITHUB_REPOSITORY
    qtVersion = $env:QT_VERSION
    architecture = 'x86_64'
    buildType = 'Release'
    commercial = $false
    grpc = $false
    webEngine = $true
    signed = $false
    sourceUrl = $sourceUrl
    workflowRun = $runUrl
} | ConvertTo-Json | Set-Content (Join-Path $root 'build-info.json') -Encoding utf8

$launcher = "@echo off`r`nstart `"`" `"%~dp0bin\$env:EXECUTABLE.exe`" %*`r`n"
Set-Content (Join-Path $root 'Serial Studio GPLv3.bat') $launcher -Encoding ascii -NoNewline
@"
Serial Studio GPLv3 $version - Windows x64 portable build

Extract the entire ZIP to a writable folder. Do NOT run the executable inside
an archive viewer and do NOT copy the EXE without its adjacent files.
Double-click "Serial Studio GPLv3.bat", or run "bin/$env:EXECUTABLE.exe".
Qt DLLs, plugins, QML modules, WebEngine resources and the MSVC runtime are
included. No Qt SDK or local compilation is required.

Windows 10/11 x64 is required. This build is unsigned; Windows SmartScreen may
show an unknown-publisher warning. Only proceed if you trust this fork.

This is the GPLv3 edition. Pro-only functionality is not compiled in and no
commercial license or activation is required. gRPC is disabled; the local
TCP/JSON API and standard GPL features remain available.

Source of this exact build: $sourceUrl
Build logs and provenance: $runUrl
See LICENSE.txt, LICENSES/ and Acknowledgements.txt for license notices.
The notices also describe dependencies used by other Serial Studio editions.

Qt $env:QT_VERSION is dynamically bundled under GPL-compatible open-source
terms. Qt source and third-party notices:
https://download.qt.io/archive/qt/6.11/$env:QT_VERSION/single/
https://doc.qt.io/qt-6/licenses-used-in-qt.html
The repository pins the remaining fetched dependencies in its CMake files.
If redistributing, provide corresponding source and comply with the licenses;
this Actions artifact is not an official Serial Studio Pro release.
"@ | Set-Content (Join-Path $root 'README.txt') -Encoding utf8

New-Item -ItemType Directory -Force 'artifacts' | Out-Null
$archive = Join-Path 'artifacts' "$name.zip"
& 7z a -tzip -mx=5 $archive "$root/*"
if ($LASTEXITCODE -ne 0) { throw 'Creating the portable ZIP failed.' }
& 7z t $archive
if ($LASTEXITCODE -ne 0) { throw 'ZIP integrity verification failed.' }

$hash = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash  $name.zip" | Set-Content 'artifacts/SHA256SUMS.txt' -Encoding ascii

# Ship the exact checked-out application and vendored source separately, never
# the build directory or any runner credentials / caches.
$source = Join-Path 'artifacts' "$env:EXECUTABLE-$version-Source-$shortSha.zip"
git archive --format=zip --output=$source $env:GITHUB_SHA
if ($LASTEXITCODE -ne 0) { throw 'Archiving the corresponding application source failed.' }

"name=$name" | Add-Content $env:GITHUB_OUTPUT -Encoding utf8
"archive=$archive" | Add-Content $env:GITHUB_OUTPUT -Encoding utf8
"source=$source" | Add-Content $env:GITHUB_OUTPUT -Encoding utf8
Write-Host "Packaged $name; SHA256: $hash"
