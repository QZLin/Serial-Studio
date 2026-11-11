# SPDX-FileCopyrightText: 2026 QZLin
# SPDX-License-Identifier: GPL-3.0-or-later
# Test the shipped ZIP in a clean, space-containing directory on GitHub Actions.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Archive
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:GITHUB_ACTIONS -ne 'true') {
    throw 'This smoke test must run on a GitHub Actions runner, not locally.'
}

$archivePath = (Resolve-Path $Archive).Path
$logDir = Join-Path $env:GITHUB_WORKSPACE 'artifacts'
$testDir = Join-Path $env:RUNNER_TEMP "Serial Studio portable smoke $([guid]::NewGuid())"
$workingDir = Join-Path $testDir 'empty working directory'
New-Item -ItemType Directory $testDir, $workingDir | Out-Null
Expand-Archive -LiteralPath $archivePath -DestinationPath $testDir
$exe = Join-Path $testDir 'bin/Serial-Studio-GPL3.exe'
if (-not (Test-Path $exe -PathType Leaf)) { throw 'No runnable GPLv3 EXE in the ZIP.' }

$originalPath = $env:PATH
$qtEnvironment = @(Get-ChildItem Env: | Where-Object {
    $_.Name -match '^(QT|QML|QTDIR|CMAKE_PREFIX_PATH)'
})
$process = $null
try {
    foreach ($entry in $qtEnvironment) { Remove-Item "Env:$($entry.Name)" }
    # Only Windows system DLLs and the deployed package can satisfy imports now.
    $env:PATH = "$env:SystemRoot\system32;$env:SystemRoot"
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_FORCE_STDERR_LOGGING = '1'

    $consoleOut = Join-Path $logDir 'console-log-selftest.stdout.log'
    $consoleErr = Join-Path $logDir 'console-log-selftest.stderr.log'
    $process = Start-Process -FilePath $exe `
        -ArgumentList '--headless', '--selftest-suite', 'console-log' `
        -WorkingDirectory $workingDir -PassThru -NoNewWindow `
        -RedirectStandardOutput $consoleOut -RedirectStandardError $consoleErr
    if (-not $process.WaitForExit(120000)) {
        $process.Kill($true)
        $process.WaitForExit()
        throw 'The packaged Console log self-test timed out.'
    }
    $process.WaitForExit()
    $consoleText = (Get-Content $consoleOut, $consoleErr -Raw) -join "`n"
    Write-Host $consoleText
    if ($process.ExitCode -ne 0) {
        throw "The packaged Console log self-test exited with $($process.ExitCode)."
    }
    if ($consoleText -notmatch '\[selftest\]\s+console-log:\s+PASS\s+\([1-9][0-9]* checks, 0 failures\)') {
        throw 'The Console log self-test did not report a real PASS verdict.'
    }
    $process = $null

    $out = Join-Path $logDir 'qml-selftest.stdout.log'
    $err = Join-Path $logDir 'qml-selftest.stderr.log'
    $process = Start-Process -FilePath $exe `
        -ArgumentList '--headless', '--selftest-suite', 'qml' `
        -WorkingDirectory $workingDir -PassThru -NoNewWindow `
        -RedirectStandardOutput $out -RedirectStandardError $err
    if (-not $process.WaitForExit(300000)) {
        $process.Kill($true)
        $process.WaitForExit()
        throw 'The packaged QML self-test timed out.'
    }
    $process.WaitForExit()
    $text = (Get-Content $out, $err -Raw) -join "`n"
    Write-Host $text
    if ($process.ExitCode -ne 0) {
        throw "The packaged QML self-test exited with $($process.ExitCode)."
    }
    if ($text -notmatch '\[selftest\]\s+qml:\s+PASS\s+\([1-9][0-9]* checks, 0 failures\)') {
        throw 'The QML self-test did not report a real PASS verdict.'
    }
    $process = $null

    # Native Windows startup proves the GUI path too, not just offscreen loading.
    $env:QT_QPA_PLATFORM = 'windows'
    $guiOut = Join-Path $logDir 'gui-startup.stdout.log'
    $guiErr = Join-Path $logDir 'gui-startup.stderr.log'
    $process = Start-Process -FilePath $exe -WorkingDirectory $workingDir `
        -PassThru -NoNewWindow `
        -RedirectStandardOutput $guiOut -RedirectStandardError $guiErr
    $deadline = [datetime]::UtcNow.AddSeconds(60)
    $windowReady = $false
    while ([datetime]::UtcNow -lt $deadline) {
        $process.Refresh()
        if ($process.HasExited) { break }
        if ($process.MainWindowHandle -ne 0) { $windowReady = $true; break }
        Start-Sleep -Milliseconds 500
    }
    if (-not $windowReady) {
        Get-Content $guiOut, $guiErr -ErrorAction SilentlyContinue | Write-Host
        throw 'The portable app did not create a native Windows GUI window.'
    }
    Start-Sleep -Seconds 5
    $process.Refresh()
    if ($process.HasExited) { throw 'The GUI exited during the startup smoke test.' }
    [void]$process.CloseMainWindow()
    if (-not $process.WaitForExit(15000)) {
        $process.Kill($true)
        $process.WaitForExit()
    }
    $process = $null
    $guiText = (Get-Content $guiOut, $guiErr -Raw) -join "`n"
    Write-Host $guiText
    if ($guiText -match 'QQmlApplicationEngine failed to load component|module "[^"]+" is not installed') {
        throw 'The GUI reported a missing QML module or failed root component.'
    }
    'PORTABLE_CONSOLE_LOG_PASS=1; PORTABLE_QML_PASS=1; PORTABLE_GUI_PASS=1; SDK_ON_PATH=0' |
        Set-Content (Join-Path $logDir 'portable-smoke-result.log') -Encoding ascii
    Write-Host 'Portable ZIP passed Console logging, QML and native GUI tests without the Qt SDK.'
} finally {
    if ($null -ne $process -and -not $process.HasExited) {
        $process.Kill($true)
        $process.WaitForExit()
    }
    $env:PATH = $originalPath
    Get-ChildItem Env: | Where-Object { $_.Name -match '^(QT|QML|QTDIR|CMAKE_PREFIX_PATH)' } |
        ForEach-Object { Remove-Item "Env:$($_.Name)" }
    foreach ($entry in $qtEnvironment) {
        Set-Item "Env:$($entry.Name)" $entry.Value
    }
}
