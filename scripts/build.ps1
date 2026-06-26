param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$preset = if ($Configuration -eq "Release") { "x64-Release" } else { "x64-Debug" }
$binary = Join-Path $root "out\build\$preset\agnc.exe"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found. Install Visual Studio 2026 with C++ workload."
}

$vsPath = & $vswhere -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) {
    throw "Visual Studio with C++ tools not found."
}

$devShell = Join-Path $vsPath "Common7\Tools\Launch-VsDevShell.ps1"
if (-not (Test-Path $devShell)) {
    throw "Launch-VsDevShell.ps1 not found at $devShell"
}

$cmake = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (-not (Test-Path $cmake)) {
    throw "CMake bundled with Visual Studio not found."
}

Write-Host "Using Visual Studio at: $vsPath"
Write-Host "Preset: $preset"
Write-Host "Output: $binary"

Push-Location $root
try {
    & $devShell -Arch amd64 -SkipAutomaticLocation | Out-Null
    & $cmake --preset $preset
    & $cmake --build --preset $preset

    if (Test-Path $binary) {
        Write-Host "Built $binary"
        & $binary --version
    } else {
        throw "Build finished but binary not found: $binary"
    }
}
finally {
    Pop-Location
}
