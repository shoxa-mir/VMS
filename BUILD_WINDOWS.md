# Building FluxVision VMS on Windows

This guide covers building FluxVision VMS on Windows 10/11 with CUDA 12.4 and NVDEC SDK.

## Prerequisites

### System Requirements
- Windows 10/11 (64-bit)
- Visual Studio 2019 or 2022 (with C++ Desktop Development workload)
- CUDA Toolkit 12.4 (installed)
- NVDEC SDK
- At least 4 GB RAM
- 15 GB free disk space

## Step 1: Install Visual Studio

### Download and Install
Download Visual Studio 2022 Community (free):
https://visualstudio.microsoft.com/downloads/

**Required Workload:**
- Desktop development with C++

**Required Components:**
- MSVC v143 (or latest)
- Windows 10/11 SDK
- C++ CMake tools for Windows

## Step 2: Install CUDA Toolkit 12.4

### Download
https://developer.nvidia.com/cuda-12-4-0-download-archive

### Installation
1. Run the installer
2. Choose "Custom" installation
3. Select:
   - CUDA Toolkit
   - CUDA Development
   - CUDA Runtime
   - CUDA Documentation (optional)

### Verify Installation
```powershell
nvcc --version
```
Should show: `release 12.4`

Default installation path: `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.4`

## Step 3: Install NVIDIA Video Codec SDK

### Download
https://developer.nvidia.com/nvidia-video-codec-sdk

### Installation
1. Extract to: `C:\Program Files\NVIDIA GPU Computing Toolkit\Video_Codec_SDK`
2. Or any location and note the path for later

**Verify:**
Check that these files exist:
- `Interface\nvcuvid.h` (or `include\nvcuvid.h`)
- `Interface\cuviddec.h`

## Step 4: Install Dependencies

### Option A: Using vcpkg (Recommended)

#### Install vcpkg
```powershell
# Clone vcpkg
cd C:\
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg

# Bootstrap
.\bootstrap-vcpkg.bat

# Set environment variable
[System.Environment]::SetEnvironmentVariable('VCPKG_ROOT', 'C:\vcpkg', 'User')
```

#### Install Dependencies
```powershell
# Qt5
.\vcpkg install qt5-base:x64-windows
.\vcpkg install qt5-opengl:x64-windows

# OpenSSL 3.x
.\vcpkg install openssl:x64-windows

# FFmpeg
.\vcpkg install ffmpeg[avcodec,avformat,avutil]:x64-windows

# SQLite3
.\vcpkg install sqlite3:x64-windows
```

### Option B: Manual Installation

#### Qt5
1. Download Qt installer: https://www.qt.io/download-qt-installer
2. Install Qt 5.15.2 with MSVC 2019 64-bit component
3. Default path: `C:\Qt\5.15.2\msvc2019_64`

#### OpenSSL
```powershell
winget install ShiningLight.OpenSSL
```
Or download from: https://slproweb.com/products/Win32OpenSSL.html
Install: Win64 OpenSSL v3.x.x

#### FFmpeg
```powershell
winget install Gyan.FFmpeg
```
Or download from: https://www.gyan.dev/ffmpeg/builds/
Extract to `C:\ffmpeg` and add `C:\ffmpeg\bin` to PATH

#### SQLite3
Download precompiled binaries: https://www.sqlite.org/download.html
- sqlite-dll-win64-x64-*.zip
- sqlite-tools-win32-x86-*.zip

Extract and add to PATH, or use vcpkg.

## Step 5: Build FluxVision VMS

### Using PowerShell Build Script (Recommended)

Open PowerShell and navigate to the project directory:

```powershell
cd E:\code\VMS\FluxVisionVMS

# Run build script
.\build_windows.ps1
```

**Custom options:**
```powershell
# Specify NVDEC SDK path
.\build_windows.ps1 -NvdecSdkPath "D:\NVIDIA\Video_Codec_SDK"

# Debug build
.\build_windows.ps1 -BuildType Debug

# Limit parallel jobs (if running out of memory)
.\build_windows.ps1 -Jobs 4
```

### Manual Build

If you prefer to build manually:

```powershell
# Create build directory
mkdir build
cd build

# Configure with CMake
cmake .. `
    -G "Visual Studio 17 2022" `
    -A x64 `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_CUDA_ARCHITECTURES="75;80;86;89" `
    -DNVDEC_SDK_PATH="C:\Program Files\NVIDIA GPU Computing Toolkit\Video_Codec_SDK"

# If using vcpkg:
cmake .. `
    -G "Visual Studio 17 2022" `
    -A x64 `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_CUDA_ARCHITECTURES="75;80;86;89" `
    -DCMAKE_TOOLCHAIN_FILE="C:\vcpkg\scripts\buildsystems\vcpkg.cmake" `
    -DNVDEC_SDK_PATH="C:\Program Files\NVIDIA GPU Computing Toolkit\Video_Codec_SDK"

# Build
cmake --build . --config Release -j 8
```

### Build Options

#### CUDA Architectures
Customize for your specific GPU:
- `75`: Turing (RTX 20 series, GTX 16 series)
- `80`: Ampere (RTX 30 series, A100)
- `86`: Ampere (RTX 3090, RTX A6000)
- `89`: Ada Lovelace (RTX 40 series)

```powershell
# For RTX 4080 SUPER only
-DCMAKE_CUDA_ARCHITECTURES="89"
```

#### Build Type
- `Release` (default): Optimized, no debug symbols
- `Debug`: Debug symbols, slower execution
- `RelWithDebInfo`: Optimized with debug symbols

## Step 6: Run the Application

### Test Hardware Detection
```powershell
.\build\win64\Release\hardware-detect.exe
```

### Test Decoders
```powershell
# Show capabilities
.\build\win64\Release\decoder-test.exe --caps

# Test all decoders
.\build\win64\Release\decoder-test.exe --test-all

# Test NVDEC only
.\build\win64\Release\decoder-test.exe --test-nvdec

# Test CPU decoder only
.\build\win64\Release\decoder-test.exe --test-cpu
```

### Run Server
```powershell
.\build\win64\Release\fluxvision-server.exe
```

### Run Client
```powershell
.\build\win64\Release\fluxvision-client.exe
```

## Troubleshooting

### CMake can't find CUDA
Ensure CUDA is in PATH:
```powershell
$env:Path += ";C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.4\bin"
```

Make permanent:
```powershell
[System.Environment]::SetEnvironmentVariable('Path',
    $env:Path + ';C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.4\bin',
    'User')
```

### CMake can't find NVDEC SDK
Specify the path explicitly:
```powershell
.\build_windows.ps1 -NvdecSdkPath "C:\path\to\Video_Codec_SDK"
```

Or set environment variable:
```powershell
$env:NVDEC_SDK_PATH = "C:\path\to\Video_Codec_SDK"
```

### CMake can't find Qt5
Set Qt5_DIR:
```powershell
$env:Qt5_DIR = "C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5"
```

Or add Qt bin to PATH:
```powershell
$env:Path += ";C:\Qt\5.15.2\msvc2019_64\bin"
```

### CMake can't find OpenSSL
```powershell
$env:OPENSSL_ROOT_DIR = "C:\Program Files\OpenSSL-Win64"
```

### Missing DLLs when running
Qt5 DLLs need to be in PATH or copied to executable directory:

**Option 1: Add to PATH**
```powershell
$env:Path += ";C:\Qt\5.15.2\msvc2019_64\bin"
```

**Option 2: Use windeployqt**
```powershell
cd build\Release
C:\Qt\5.15.2\msvc2019_64\bin\windeployqt.exe fluxvision-client.exe
```

### NVDEC Library Not Found
The build script looks for `nvcuvid.lib` in CUDA toolkit directories. If not found:

1. Ensure NVIDIA driver is installed (libnvcuvid comes with driver)
2. Check: `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.4\lib\x64\nvcuvid.lib`
3. If missing, reinstall CUDA or NVIDIA driver

### Build fails with "Out of memory"
Reduce parallel jobs:
```powershell
.\build_windows.ps1 -Jobs 4
```

### Visual Studio version mismatch
Specify Visual Studio version:
```powershell
cmake .. -G "Visual Studio 16 2019" -A x64  # VS 2019
cmake .. -G "Visual Studio 17 2022" -A x64  # VS 2022
```

## Environment Setup Script

Create `setup_env.ps1` for persistent environment:

```powershell
# setup_env.ps1
# CUDA
$env:CUDA_PATH = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v12.4"
$env:Path += ";$env:CUDA_PATH\bin"

# NVDEC SDK
$env:NVDEC_SDK_PATH = "C:\Program Files\NVIDIA GPU Computing Toolkit\Video_Codec_SDK"

# Qt5
$env:Qt5_DIR = "C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5"
$env:Path += ";C:\Qt\5.15.2\msvc2019_64\bin"

# OpenSSL
$env:OPENSSL_ROOT_DIR = "C:\Program Files\OpenSSL-Win64"

# vcpkg (if using)
$env:VCPKG_ROOT = "C:\vcpkg"

Write-Host "Environment configured for FluxVision VMS build" -ForegroundColor Green
```

Then run before building:
```powershell
.\setup_env.ps1
.\build_windows.ps1
```

## Build Artifacts

After successful build:

```
build\
└── win64\
    ├── Release\
    │   ├── fluxvision-server.exe       # Server executable
    │   ├── fluxvision-client.exe       # Client executable
    │   ├── hardware-detect.exe         # Hardware detection tool
    │   ├── decoder-test.exe            # Decoder test tool
    │   ├── fluxvision-core.lib         # Core library
    │   ├── fluxvision-common.lib       # Common library
    │   └── fluxvision-recording.lib    # Recording library
    └── compile_commands.json           # For IDE integration
```

## Installation

### System-wide installation
```powershell
cmake --install build\win64 --config Release
```

Default install location: `C:\Program Files\FluxVision VMS\`

### Portable installation
Copy `build\win64\Release\` contents to desired location along with required DLLs.

## Next Steps

1. Configure the system: Edit `config\config.yaml`
2. Set up cameras and storage
3. Run the server
4. Connect with the client
5. See [PHASE1_COMPLETION.md](PHASE1_COMPLETION.md) for Phase 1 details

## Quick Reference

```powershell
# One-command build
.\build_windows.ps1

# Clean build
Remove-Item -Recurse -Force build
.\build_windows.ps1

# Debug build
.\build_windows.ps1 -BuildType Debug

# Custom NVDEC path
.\build_windows.ps1 -NvdecSdkPath "D:\NVIDIA\Video_Codec_SDK"

# Test
.\build\win64\Release\decoder-test.exe --test-all

# Install
cmake --install build\win64 --config Release
```

## Additional Resources

- **CUDA Toolkit**: https://developer.nvidia.com/cuda-downloads
- **NVDEC SDK**: https://developer.nvidia.com/nvidia-video-codec-sdk
- **Qt5**: https://www.qt.io/download
- **Visual Studio**: https://visualstudio.microsoft.com/
- **vcpkg**: https://vcpkg.io/
