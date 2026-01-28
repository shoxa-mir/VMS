#!/bin/bash
# Build script for FluxVision VMS in WSL with CUDA 12.4
# Run this script from WSL environment

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=================================================${NC}"
echo -e "${GREEN}FluxVision VMS - WSL Build Script${NC}"
echo -e "${GREEN}=================================================${NC}"

# Configuration
CUDA_VERSION="12.4"
NVDEC_SDK_PATH="/opt/nvidia/nvidia-codec-sdk"
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_DIR="build/linux64"
NUM_JOBS=$(nproc)

echo -e "\n${YELLOW}Configuration:${NC}"
echo "  CUDA Version: $CUDA_VERSION"
echo "  NVDEC SDK Path: $NVDEC_SDK_PATH"
echo "  Build Type: $BUILD_TYPE"
echo "  Build Directory: $BUILD_DIR"
echo "  Parallel Jobs: $NUM_JOBS"

# Check CUDA installation
echo -e "\n${YELLOW}Checking CUDA installation...${NC}"
if command -v nvcc &> /dev/null; then
    NVCC_VERSION=$(nvcc --version | grep "release" | awk '{print $5}' | cut -d',' -f1)
    echo -e "${GREEN}✓${NC} CUDA found: $NVCC_VERSION"
else
    echo -e "${RED}✗${NC} CUDA not found. Please ensure CUDA 12.4 is installed and in PATH"
    exit 1
fi

# Check NVDEC SDK
echo -e "\n${YELLOW}Checking NVDEC SDK...${NC}"
if [ -d "$NVDEC_SDK_PATH/include" ] || [ -d "$NVDEC_SDK_PATH/Interface" ]; then
    echo -e "${GREEN}✓${NC} NVDEC SDK found at $NVDEC_SDK_PATH"
    export NVDEC_SDK_PATH="$NVDEC_SDK_PATH"
else
    echo -e "${RED}✗${NC} NVDEC SDK not found at $NVDEC_SDK_PATH"
    echo "  Expected directories: $NVDEC_SDK_PATH/include or $NVDEC_SDK_PATH/Interface"
    echo "  You can download it from: https://developer.nvidia.com/nvidia-video-codec-sdk"
    exit 1
fi

# Check for required dependencies
echo -e "\n${YELLOW}Checking dependencies...${NC}"

# Qt5
if pkg-config --exists Qt5Core Qt5Widgets Qt5OpenGL Qt5Network; then
    QT_VERSION=$(pkg-config --modversion Qt5Core)
    echo -e "${GREEN}✓${NC} Qt5 found: $QT_VERSION"
else
    echo -e "${RED}✗${NC} Qt5 not found. Install with:"
    echo "  sudo apt install qtbase5-dev qtbase5-dev-tools"
    exit 1
fi

# OpenSSL
if pkg-config --exists openssl; then
    OPENSSL_VERSION=$(pkg-config --modversion openssl)
    echo -e "${GREEN}✓${NC} OpenSSL found: $OPENSSL_VERSION"

    # Check if OpenSSL version is 3.0+
    OPENSSL_MAJOR=$(echo $OPENSSL_VERSION | cut -d'.' -f1)
    if [ "$OPENSSL_MAJOR" -lt 3 ]; then
        echo -e "${YELLOW}⚠${NC}  OpenSSL 3.0+ required, found $OPENSSL_VERSION"
        echo "  Install OpenSSL 3.0+ or build from source"
    fi
else
    echo -e "${RED}✗${NC} OpenSSL not found. Install with:"
    echo "  sudo apt install libssl-dev"
    exit 1
fi

# FFmpeg
if pkg-config --exists libavformat libavcodec libavutil; then
    FFMPEG_VERSION=$(pkg-config --modversion libavformat)
    echo -e "${GREEN}✓${NC} FFmpeg found: $FFMPEG_VERSION"
else
    echo -e "${YELLOW}⚠${NC}  FFmpeg not found (optional but recommended)"
    echo "  Install with: sudo apt install libavformat-dev libavcodec-dev libavutil-dev"
fi

# SQLite3
if pkg-config --exists sqlite3; then
    SQLITE_VERSION=$(pkg-config --modversion sqlite3)
    echo -e "${GREEN}✓${NC} SQLite3 found: $SQLITE_VERSION"
else
    echo -e "${RED}✗${NC} SQLite3 not found. Install with:"
    echo "  sudo apt install libsqlite3-dev"
    exit 1
fi

# Create build directory
echo -e "\n${YELLOW}Creating build directory...${NC}"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with CMake
echo -e "\n${YELLOW}Configuring with CMake...${NC}"
cmake ../.. \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_CUDA_ARCHITECTURES="75;80;86;89" \
    -DNVDEC_SDK_PATH="$NVDEC_SDK_PATH" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

if [ $? -ne 0 ]; then
    echo -e "\n${RED}CMake configuration failed!${NC}"
    exit 1
fi

# Build
echo -e "\n${YELLOW}Building FluxVision VMS...${NC}"
cmake --build . --config "$BUILD_TYPE" -j "$NUM_JOBS"

if [ $? -ne 0 ]; then
    echo -e "\n${RED}Build failed!${NC}"
    exit 1
fi

# Success
echo -e "\n${GREEN}=================================================${NC}"
echo -e "${GREEN}Build completed successfully!${NC}"
echo -e "${GREEN}=================================================${NC}"
echo -e "\nBinaries are located in: $BUILD_DIR/"
echo -e "\nTo run the server:"
echo -e "  ./$BUILD_DIR/src/server/fluxvision-server"
echo -e "\nTo run the client:"
echo -e "  ./$BUILD_DIR/src/client/fluxvision-client"
echo -e "\nTo install system-wide (optional):"
echo -e "  sudo cmake --install $BUILD_DIR"
