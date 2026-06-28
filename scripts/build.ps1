param(
    # Posisi 0: .\scripts\build.ps1 release | debug
    [Parameter(Position = 0)]
    [ValidateSet("debug", "release", "")]
    [string]$BuildType = "",

    [ValidateSet("Debug", "Release")]
    [string]$Configuration = ""
)

$ErrorActionPreference = "Stop"

if ($BuildType -eq "release") {
    $Configuration = "Release"
} elseif ($BuildType -eq "debug") {
    $Configuration = "Debug"
} elseif ($Configuration -eq "") {
    $Configuration = "Debug"
}

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
Write-Host "Configuration: $Configuration"
Write-Host "Preset: $preset"
Write-Host "Output: $binary"

if ($Configuration -eq "Release") {
    Write-Host "Release: vcpkg hanya build dependency release (bukan grpc debug)."
} elseif ($Configuration -eq "Debug") {
    Write-Host "Debug: vcpkg hanya build dependency debug."
}

$env:VCPKG_ROOT = Join-Path $vsPath "VC\vcpkg"
$toolchain = Join-Path $env:VCPKG_ROOT "scripts\buildsystems\vcpkg.cmake"
Write-Host "VCPKG_ROOT: $env:VCPKG_ROOT"

$cmakeConfigureArgs = @(
    "--preset", $preset,
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DVCPKG_MANIFEST_MODE=ON"
)
if ($Configuration -eq "Release") {
    $cmakeConfigureArgs += "-DVCPKG_BUILD_TYPE=release"
} elseif ($Configuration -eq "Debug") {
    $cmakeConfigureArgs += "-DVCPKG_BUILD_TYPE=debug"
}

Push-Location $root
try {
    & $devShell -Arch amd64 -SkipAutomaticLocation | Out-Null
    Write-Host "Generating provider registry..."
    python (Join-Path $root "scripts\generate_integrations.py")
    & $cmake @cmakeConfigureArgs
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
