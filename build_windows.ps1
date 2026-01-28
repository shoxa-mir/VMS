# build_windows.ps1
# Build script for FluxVision VMS on Windows with CUDA 12.4

param(
    [string]$BuildType = "Release",
    [string]$NvdecSdkPath = "C:\Program Files\NVIDIA GPU Computing Toolkit\Video_Codec_SDK",
    [int]$Jobs = 0
)

# Enable strict error handling
$ErrorActionPreference = "Stop"

# Colors
function Write-Success { Write-Host $args -ForegroundColor Green }
function Write-Info { Write-Host $args -ForegroundColor Cyan }
function Write-Warn { Write-Host $args -ForegroundColor Yellow }
function Write-Fail { Write-Host $args -ForegroundColor Red }

Write-Success "================================================="
Write-Success "FluxVision VMS - Windows Build Script"
Write-Success "================================================="

# Configuration
$CUDA_VERSION = "12.4"
$BUILD_DIR = "build\win64"
if ($Jobs -eq 0) {
    $Jobs = (Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors
}

Write-Info "`nConfiguration:"
Write-Host "  CUDA Version: $CUDA_VERSION"
Write-Host "  NVDEC SDK Path: $NvdecSdkPath"
Write-Host "  Build Type: $BuildType"
Write-Host "  Build Directory: $BUILD_DIR"
Write-Host "  Parallel Jobs: $Jobs"

# Check CUDA installation
Write-Info "`nChecking CUDA installation..."
$nvccPath = Get-Command nvcc -ErrorAction SilentlyContinue
if ($nvccPath) {
    $cudaVersion = & nvcc --version 2>&1 | Select-String "release (\d+\.\d+)" | ForEach-Object { $_.Matches[0].Groups[1].Value }
    Write-Success "✓ CUDA found: $cudaVersion"

    # Verify it's CUDA 12.4
    if (-not $cudaVersion.StartsWith("12.")) {
        Write-Warn "⚠ CUDA $cudaVersion found, but 12.x recommended"
    }
} else {
    Write-Fail "✗ CUDA not found. Please ensure CUDA 12.4 is installed and in PATH"
    Write-Host "  Download from: https://developer.nvidia.com/cuda-downloads"
    exit 1
}

# Check NVDEC SDK
Write-Info "`nChecking NVDEC SDK..."
if ((Test-Path "$NvdecSdkPath\Interface\nvcuvid.h") -or (Test-Path "$NvdecSdkPath\include\nvcuvid.h")) {
    Write-Success "✓ NVDEC SDK found at $NvdecSdkPath"
    $env:NVDEC_SDK_PATH = $NvdecSdkPath
} else {
    Write-Fail "✗ NVDEC SDK not found at $NvdecSdkPath"
    Write-Host "  Expected: $NvdecSdkPath\Interface\nvcuvid.h or $NvdecSdkPath\include\nvcuvid.h"
    Write-Host "  Download from: https://developer.nvidia.com/nvidia-video-codec-sdk"
    Write-Host "  Or specify path with: -NvdecSdkPath <path>"
    exit 1
}

# Check for required dependencies
Write-Info "`nChecking dependencies..."

# Qt5
$qtFound = $false
$qtPaths = @(
    "C:\Qt\5.15.19\msvc2019_64",
    "C:\Qt\5.15.2\msvc2019_64",
    "C:\Qt\5.15\msvc2019_64",
    "C:\Qt\Qt5.15.2\5.15.2\msvc2019_64",
    "C:\Qt\6.0\msvc2019_64"
)

foreach ($qtPath in $qtPaths) {
    if (Test-Path "$qtPath\bin\qmake.exe") {
        $env:Qt5_DIR = "$qtPath\lib\cmake\Qt5"
        $env:PATH = "$qtPath\bin;$env:PATH"
        Write-Success "✓ Qt5 found: $qtPath"
        $qtFound = $true
        break
    }
}

if (-not $qtFound) {
    Write-Fail "✗ Qt5 not found. Install from:"
    Write-Host "  https://www.qt.io/download-qt-installer"
    Write-Host "  Or set Qt5_DIR environment variable"
    exit 1
}

# OpenSSL (vcpkg or system)
$opensslFound = $false
$opensslPaths = @(
    "C:\Program Files\OpenSSL-Win64",
    "C:\OpenSSL-Win64",
    "$env:VCPKG_ROOT\installed\x64-windows"
)

foreach ($sslPath in $opensslPaths) {
    if (Test-Path "$sslPath\include\openssl\ssl.h") {
        $env:OPENSSL_ROOT_DIR = $sslPath
        Write-Success "✓ OpenSSL found: $sslPath"
        $opensslFound = $true
        break
    }
}

if (-not $opensslFound) {
    Write-Warn "⚠ OpenSSL not found (optional but recommended)"
    Write-Host "  Install with: winget install ShiningLight.OpenSSL"
    Write-Host "  Or use vcpkg: vcpkg install openssl:x64-windows"
}

# FFmpeg (optional)
$ffmpegFound = Get-Command ffmpeg -ErrorAction SilentlyContinue
if ($ffmpegFound) {
    Write-Success "✓ FFmpeg found"
} else {
    Write-Warn "⚠ FFmpeg not found (optional but recommended)"
    Write-Host "  Install with: winget install Gyan.FFmpeg"
}

# SQLite3 (vcpkg recommended)
$sqlite3Found = $false
if ($env:VCPKG_ROOT) {
    $vcpkgSqlite = "$env:VCPKG_ROOT\installed\x64-windows\include\sqlite3.h"
    if (Test-Path $vcpkgSqlite) {
        Write-Success "✓ SQLite3 found (vcpkg)"
        $sqlite3Found = $true
    }
}

if (-not $sqlite3Found) {
    Write-Warn "⚠ SQLite3 not found. Install with:"
    Write-Host "  vcpkg install sqlite3:x64-windows"
    Write-Host "  Or download from: https://www.sqlite.org/download.html"
}

# Check for Visual Studio
Write-Info "`nChecking Visual Studio..."
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vsWhere) {
    $vsPath = & $vsWhere -latest -property installationPath
    if ($vsPath) {
        Write-Success "✓ Visual Studio found: $vsPath"
    }
} else {
    Write-Warn "⚠ Visual Studio not found (may need manual setup)"
}

# Create build directory
Write-Info "`nCreating build directory..."
if (Test-Path $BUILD_DIR) {
    Remove-Item -Recurse -Force $BUILD_DIR
}
New-Item -ItemType Directory -Path $BUILD_DIR | Out-Null

# Configure with CMake
Write-Info "`nConfiguring with CMake..."
Push-Location $BUILD_DIR

try {
    $cmakeArgs = @(
        "..\\..",
        "-G", "Visual Studio 17 2022",
        "-A", "x64",
        "-DCMAKE_BUILD_TYPE=$BuildType",
        "-DCMAKE_CUDA_ARCHITECTURES=75;80;86;89"
    )

    if ($env:NVDEC_SDK_PATH) {
        $cmakeArgs += "-DNVDEC_SDK_PATH=$env:NVDEC_SDK_PATH"
    }

    if ($env:OPENSSL_ROOT_DIR) {
        $cmakeArgs += "-DOPENSSL_ROOT_DIR=$env:OPENSSL_ROOT_DIR"
    }

    if ($env:VCPKG_ROOT) {
        $cmakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"
    }

    & cmake $cmakeArgs

    if ($LASTEXITCODE -ne 0) {
        Write-Fail "`nCMake configuration failed!"
        exit 1
    }

    # Build
    Write-Info "`nBuilding FluxVision VMS..."
    & cmake --build . --config $BuildType -j $Jobs

    if ($LASTEXITCODE -ne 0) {
        Write-Fail "`nBuild failed!"
        exit 1
    }

} finally {
    Pop-Location
}

# Success
Write-Success "`n================================================="
Write-Success "Build completed successfully!"
Write-Success "================================================="
Write-Host "`nBinaries are located in: $BUILD_DIR\$BuildType\"
Write-Host "`nTo run the server:"
Write-Host "  .\$BUILD_DIR\$BuildType\fluxvision-server.exe"
Write-Host "`nTo run the client:"
Write-Host "  .\$BUILD_DIR\$BuildType\fluxvision-client.exe"
Write-Host "`nTo run decoder test:"
Write-Host "  .\$BUILD_DIR\$BuildType\decoder-test.exe --test-all"
Write-Host "`nTo install system-wide (optional):"
Write-Host "  cmake --install $BUILD_DIR --config $BuildType"
