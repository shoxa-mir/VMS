# Building FluxVision VMS in WSL

This guide covers building FluxVision VMS in Windows Subsystem for Linux (WSL) with CUDA 12.4 and NVDEC SDK.

## Prerequisites

### System Requirements
- WSL 2 (Ubuntu 20.04+ or Debian 11+)
- CUDA Toolkit 12.4 (already installed at your system)
- NVDEC SDK (installed at `/opt/nvidia/nvidia-codec-sdk`)
- At least 4 GB RAM
- 10 GB free disk space

## Step 1: Install Required Dependencies

### Update Package Lists
```bash
sudo apt update
```

### Install Build Tools
```bash
sudo apt install -y build-essential cmake git pkg-config
```

### Install Qt5 (Required)
```bash
sudo apt install -y qtbase5-dev qtbase5-dev-tools libqt5opengl5-dev
```

### Install OpenSSL 3.0+ (Required)
Check your OpenSSL version:
```bash
openssl version
```

If you have OpenSSL 3.0+, install development headers:
```bash
sudo apt install -y libssl-dev
```

**If you have OpenSSL 1.x**, you need to upgrade:

#### Ubuntu 22.04+ / Debian 12+
```bash
sudo apt install -y libssl-dev
```

#### Ubuntu 20.04 / Debian 11 (OpenSSL 1.x)
You'll need to build OpenSSL 3.x from source or upgrade to Ubuntu 22.04:
```bash
# Download OpenSSL 3.x
cd /tmp
wget https://www.openssl.org/source/openssl-3.0.13.tar.gz
tar -xzf openssl-3.0.13.tar.gz
cd openssl-3.0.13

# Build and install
./config --prefix=/usr/local/openssl-3 --openssldir=/usr/local/openssl-3
make -j$(nproc)
sudo make install

# Update pkg-config path
export PKG_CONFIG_PATH=/usr/local/openssl-3/lib/pkgconfig:$PKG_CONFIG_PATH
export LD_LIBRARY_PATH=/usr/local/openssl-3/lib:$LD_LIBRARY_PATH
```

### Install FFmpeg (Recommended)
```bash
sudo apt install -y libavformat-dev libavcodec-dev libavutil-dev
```

### Install SQLite3 (Required)
```bash
sudo apt install -y libsqlite3-dev
```

### Verify CUDA Installation
```bash
nvcc --version
```
Should show CUDA 12.4. If not, ensure CUDA is in your PATH:
```bash
export PATH=/usr/local/cuda-12.4/bin:$PATH
export LD_LIBRARY_PATH=/usr/local/cuda-12.4/lib64:$LD_LIBRARY_PATH
```

Add these to your `~/.bashrc` to make them permanent:
```bash
echo 'export NVDEC_SDK_PATH=/opt/nvidia/nvidia-codec-sdk' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH=/usr/local/cuda-12.4/lib64:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc
```

### Verify NVDEC SDK
```bash
ls -la /opt/nvidia/nvidia-codec-sdk
```

The directory should contain either an `include/` or `Interface/` subdirectory with headers like `nvcuvid.h`.

## Step 2: Build FluxVision VMS

### Method 1: Using the Build Script (Recommended)

Navigate to the project directory and make the build script executable:
```bash
cd /mnt/e/code/VMS/FluxVisionVMS  # Or wherever your repo is in WSL
chmod +x build_wsl.sh
```

Run the build script:
```bash
./build_wsl.sh
```

The script will:
- ✅ Check all dependencies
- ✅ Configure the NVDEC SDK path
- ✅ Run CMake configuration
- ✅ Build the project with all CPU cores

### Method 2: Manual Build

If you prefer to build manually:

```bash
# Set NVDEC SDK path
export NVDEC_SDK_PATH=/opt/nvidia/nvidia-codec-sdk

# Create build directory
mkdir -p build
cd build

# Configure with CMake
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CUDA_ARCHITECTURES="75;80;86;89" \
    -DNVDEC_SDK_PATH=$NVDEC_SDK_PATH

# Build (use all CPU cores)
cmake --build . -j$(nproc)
```

### Build Options

#### Build Type
- `Release` (default): Optimized for performance
- `Debug`: Includes debug symbols, slower execution

```bash
# For debug build:
BUILD_TYPE=Debug ./build_wsl.sh

# Or with manual build:
cmake .. -DCMAKE_BUILD_TYPE=Debug
```

#### CUDA Architectures
The build script targets common architectures:
- `75`: Turing (RTX 20 series, GTX 16 series)
- `80`: Ampere (RTX 30 series, A100)
- `86`: Ampere (RTX 3090, RTX A6000)
- `89`: Ada Lovelace (RTX 40 series)

Customize for your specific GPU:
```bash
cmake .. -DCMAKE_CUDA_ARCHITECTURES="89"  # For RTX 4090
```

## Step 3: Run the Application

### Server
```bash
./build/linux64/src/server/fluxvision-server
```

### Client
```bash
./build/linux64/src/client/fluxvision-client
```

### Test Decoders (Phase 1)
```bash
./build/linux64/tools/decoder-test --test-all
./build/linux64/tools/hardware-detect
```

## Step 4: Install (Optional)

To install system-wide:
```bash
sudo cmake --install build/linux64
```

This will install to `/opt/fluxvision/` by default.

## Troubleshooting

### CMake can't find CUDA
```bash
# Ensure CUDA is in PATH
export PATH=/usr/local/cuda-12.4/bin:$PATH
export CUDA_HOME=/usr/local/cuda-12.4
```

### CMake can't find NVDEC SDK
The build script sets `NVDEC_SDK_PATH` automatically. If building manually:
```bash
export NVDEC_SDK_PATH=/opt/nvidia/nvidia-codec-sdk
```

Or specify directly in CMake:
```bash
cmake .. -DNVDEC_SDK_PATH=/opt/nvidia/nvidia-codec-sdk
```

### Qt5 not found
```bash
# Install Qt5
sudo apt install qtbase5-dev qtbase5-dev-tools

# If still not found, specify Qt path:
export Qt5_DIR=/usr/lib/x86_64-linux-gnu/cmake/Qt5
```

### OpenSSL 3.0 not found
If you built OpenSSL from source:
```bash
export PKG_CONFIG_PATH=/usr/local/openssl-3/lib/pkgconfig:$PKG_CONFIG_PATH
cmake .. -DOPENSSL_ROOT_DIR=/usr/local/openssl-3
```

### FFmpeg not found
FFmpeg is optional but recommended for RTSP support:
```bash
sudo apt install libavformat-dev libavcodec-dev libavutil-dev
```

### Build fails with CUDA errors
Ensure your CUDA version matches:
```bash
nvcc --version  # Should show 12.4
```

Check GPU compute capability and adjust `CMAKE_CUDA_ARCHITECTURES` accordingly.

### Memory issues during build
Reduce parallel jobs:
```bash
cmake --build build -j2  # Use only 2 cores instead of all
```

## Build Artifacts

After a successful build, you'll find:

```
build/
├── src/
│   ├── server/
│   │   └── fluxvision-server          # Server executable
│   ├── client/
│   │   └── fluxvision-client          # Client executable
│   └── ...
└── compile_commands.json              # For IDE integration
```

## Next Steps

1. Configure the system: Edit `config/config.yaml`
2. Set up cameras and storage
3. Run the server
4. Connect with the client
5. See [PHASE0_COMPLETION.md](PHASE0_COMPLETION.md) for more details

## Additional Resources

- **CUDA Toolkit**: https://developer.nvidia.com/cuda-downloads
- **NVDEC SDK**: https://developer.nvidia.com/nvidia-video-codec-sdk
- **Qt5 Documentation**: https://doc.qt.io/qt-5/
- **OpenSSL**: https://www.openssl.org/

## Quick Reference

```bash
# One-command build
./build_wsl.sh

# Clean build
rm -rf build && ./build_wsl.sh

# Debug build
BUILD_TYPE=Debug ./build_wsl.sh

# Install
sudo cmake --install build

# Uninstall
sudo rm -rf /opt/fluxvision
```
