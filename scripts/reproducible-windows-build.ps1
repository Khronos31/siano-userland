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
$sourceFiles = @(& git -C $root ls-files)
if ($LASTEXITCODE -ne 0) { throw 'cannot enumerate tracked C/C++ build inputs' }
$programs = @('sianod.exe', 'sianoctl.exe', 'siano-ts.exe')

function New-CleanBuild([string]$name) {
    $build = Join-Path $buildRoot $name
    New-Item -ItemType Directory -Force -Path $build | Out-Null

    foreach ($file in $sourceFiles) {
        $source = Join-Path $root $file
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "required Windows build input is missing: $source"
        }
        $destination = Join-Path $build $file
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination
    }

    $libusb = Join-Path $root 'libusb'
    if (-not (Test-Path -LiteralPath $libusb -PathType Container)) {
        throw "required libusb directory is missing: $libusb"
    }
    Copy-Item -LiteralPath $libusb -Destination $build -Recurse -Force
    foreach ($dependency in @('dtv-userland-core', 'px4-userland')) {
        $inputRoot = Join-Path $root "third_party/$dependency"
        if (-not (Test-Path -LiteralPath $inputRoot -PathType Container)) {
            throw "missing pinned dependency: $inputRoot"
        }
        $outputRoot = Join-Path $build "third_party/$dependency"
        New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
        foreach ($file in @(& git -C $inputRoot ls-files)) {
            $target = Join-Path $outputRoot $file
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            Copy-Item -LiteralPath (Join-Path $inputRoot $file) -Destination $target
        }
        if ($LASTEXITCODE -ne 0) { throw "cannot enumerate $dependency inputs" }
    }

    Push-Location $build
    $savedErrorActionPreference = $ErrorActionPreference
    try {
        # Windows PowerShell wraps normal native stderr (including nmake's
        # banner) as ErrorRecords. The native exit status decides build success.
        $ErrorActionPreference = 'Continue'
        $nmakeOutput = & nmake /f Makefile.win 2>&1
        $nmakeExitCode = $LASTEXITCODE
        $ErrorActionPreference = $savedErrorActionPreference
        $nmakeOutput | ForEach-Object { Write-Host $_ }
        if ($nmakeExitCode -ne 0) {
            throw "nmake failed for clean build $name with exit code $nmakeExitCode"
        }
    } finally {
        $ErrorActionPreference = $savedErrorActionPreference
        Pop-Location
    }

    foreach ($program in $programs) {
        if (-not (Test-Path -LiteralPath (Join-Path $build $program) -PathType Leaf)) {
            throw "clean build $name did not produce $program"
        }
    }
    return $build
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
foreach ($program in $programs) {
$firstBytes = [System.IO.File]::ReadAllBytes((Join-Path $first $program))
$secondBytes = [System.IO.File]::ReadAllBytes((Join-Path $second $program))
if ($firstBytes.Length -ne $secondBytes.Length) {
    throw "Windows clean builds differ in size: $($firstBytes.Length) vs $($secondBytes.Length) bytes"
}
for ($index = 0; $index -lt $firstBytes.Length; $index++) {
    if ($firstBytes[$index] -ne $secondBytes[$index]) {
        throw "Windows clean builds differ at byte offset $index"
    }
}

$output = Join-Path $root $program
Copy-Item -LiteralPath (Join-Path $second $program) -Destination $output -Force
$actualHash = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant()
Write-Host "$program clean builds are byte-identical ($($secondBytes.Length) bytes), SHA-256 $actualHash"
}

# The checked-in digest belongs to the historical C standalone graph. Keep it
# as historical evidence; it is not a digest for the new three-program graph.
$baselinePath = Join-Path $root 'packaging/windows-baseline.sha256'
if (-not (Test-Path -LiteralPath $baselinePath -PathType Leaf)) {
    throw "Windows baseline file is missing: $baselinePath"
}
$baselineText = [System.IO.File]::ReadAllText($baselinePath).Trim()
if ($baselineText -notmatch '^(?<hash>[0-9a-fA-F]{64})  siano-ts\.exe$') {
    throw "Windows baseline file has invalid format: $baselinePath"
}
$legacyBaselineHash = $Matches['hash'].ToLowerInvariant()
Write-Host "Historical standalone baseline retained (different source graph): $legacyBaselineHash"
