#!/usr/bin/env pwsh
# SPDX-License-Identifier: GPL-2.0-or-later
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$tempRoot = if ([string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) {
    [System.IO.Path]::GetTempPath()
} else {
    $env:RUNNER_TEMP
}
$buildRoot = Join-Path $tempRoot 'siano-windows-reproducibility'
$sourceFiles = @(
    'Makefile.win',
    'protocol.c',
    'protocol.h',
    'siano-clock.h',
    'siano-os.h',
    'siano-ts.c',
    'stream-state.c',
    'stream-state.h'
)

function New-CleanBuild([string]$name) {
    $build = Join-Path $buildRoot $name
    New-Item -ItemType Directory -Force -Path $build | Out-Null

    foreach ($file in $sourceFiles) {
        $source = Join-Path $root $file
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "required Windows build input is missing: $source"
        }
        Copy-Item -LiteralPath $source -Destination (Join-Path $build $file)
    }

    $libusb = Join-Path $root 'libusb'
    if (-not (Test-Path -LiteralPath $libusb -PathType Container)) {
        throw "required libusb directory is missing: $libusb"
    }
    Copy-Item -LiteralPath $libusb -Destination $build -Recurse -Force

    Push-Location $build
    try {
        $nmakeOutput = & nmake /f Makefile.win 2>&1
        $nmakeExitCode = $LASTEXITCODE
        $nmakeOutput | ForEach-Object { Write-Host $_ }
        if ($nmakeExitCode -ne 0) {
            throw "nmake failed for clean build $name with exit code $nmakeExitCode"
        }
    } finally {
        Pop-Location
    }

    $binary = Join-Path $build 'siano-ts.exe'
    if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
        throw "clean build $name did not produce $binary"
    }
    return $binary
}

if (Test-Path -LiteralPath $buildRoot) {
    Remove-Item -LiteralPath $buildRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null

$first = New-CleanBuild 'build-1'
# COFF/PE timestamps have one-second granularity; wait two seconds so a
# timestamp-based build would necessarily differ before starting build-2.
Start-Sleep -Seconds 2
$second = New-CleanBuild 'build-2'
$firstBytes = [System.IO.File]::ReadAllBytes($first)
$secondBytes = [System.IO.File]::ReadAllBytes($second)
if ($firstBytes.Length -ne $secondBytes.Length) {
    throw "Windows clean builds differ in size: $($firstBytes.Length) vs $($secondBytes.Length) bytes"
}
for ($index = 0; $index -lt $firstBytes.Length; $index++) {
    if ($firstBytes[$index] -ne $secondBytes[$index]) {
        throw "Windows clean builds differ at byte offset $index"
    }
}

Copy-Item -LiteralPath $second -Destination (Join-Path $root 'siano-ts.exe') -Force
Write-Host "Windows clean builds are byte-identical ($($secondBytes.Length) bytes)"
