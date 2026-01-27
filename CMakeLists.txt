# FluxVision VMS v2 - Root CMakeLists.txt
# Cross-platform Video Management System with KCMVP encryption

cmake_minimum_required(VERSION 3.18)
project(FluxVisionVMS VERSION 2.0.0 LANGUAGES CXX CUDA)

# C++ and CUDA standards
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Platform detection
if(WIN32)
    set(PLATFORM_NAME "Windows")
    add_definitions(-DPLATFORM_WINDOWS)

    # Force MSVC on Windows (CUDA requires MSVC)
    if(NOT MSVC)
        message(FATAL_ERROR "Windows builds require MSVC compiler for CUDA compatibility. MinGW is NOT supported.")
    endif()

    # Windows-specific compiler flags
    add_compile_options(/W4 /MP /EHsc)  # Warning level 4, Multi-processor, Exception handling
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    # Disable specific warnings
    add_compile_options(/wd4100)  # Unreferenced formal parameter

elseif(UNIX AND NOT APPLE)
    set(PLATFORM_NAME "Linux")
    add_definitions(-DPLATFORM_LINUX)

    # Linux-specific compiler flags
    add_compile_options(-Wall -Wextra -Wpedantic -pthread)
    set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

else()
    message(FATAL_ERROR "Unsupported platform. Only Linux and Windows are supported.")
endif()

message(STATUS "")
message(STATUS "===================================================")
message(STATUS "  FluxVision VMS v${PROJECT_VERSION}")
message(STATUS "  Platform: ${PLATFORM_NAME}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "===================================================")
message(STATUS "")

# CMake module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Find CUDA Toolkit (mandatory, same on both platforms)
find_package(CUDAToolkit 12.0 REQUIRED)
if(CUDAToolkit_FOUND)
    message(STATUS "CUDA Toolkit: ${CUDAToolkit_VERSION}")
    message(STATUS "CUDA Include: ${CUDAToolkit_INCLUDE_DIRS}")
    message(STATUS "CUDA Libraries: ${CUDAToolkit_LIBRARY_DIR}")
else()
    message(FATAL_ERROR "CUDA Toolkit 12.0+ not found. Please install from https://developer.nvidia.com/cuda-downloads")
endif()

# Find NVDEC (Video Codec SDK)
find_package(NVDEC)
if(NVDEC_FOUND)
    message(STATUS "NVDEC SDK: Found")
    message(STATUS "NVDEC Include: ${NVDEC_INCLUDE_DIR}")
else()
    message(WARNING "NVDEC SDK not found. Download from https://developer.nvidia.com/nvidia-video-codec-sdk")
    message(WARNING "Hardware video decoding will not be available.")
endif()

# Find Qt5 (mandatory, cross-platform GUI)
find_package(Qt5 REQUIRED COMPONENTS Core Widgets OpenGL Network)
if(Qt5_FOUND)
    message(STATUS "Qt5: ${Qt5_VERSION}")
    message(STATUS "Qt5 Core: ${Qt5Core_VERSION}")
    message(STATUS "Qt5 Widgets: ${Qt5Widgets_VERSION}")
    message(STATUS "Qt5 OpenGL: ${Qt5OpenGL_VERSION}")
else()
    message(FATAL_ERROR "Qt5 not found. Please install Qt 5.15+")
endif()

# Find OpenSSL 3.0+ (for ARIA, ECDH, HKDF)
find_package(OpenSSL 3.0 REQUIRED)
if(OPENSSL_FOUND)
    message(STATUS "OpenSSL: ${OPENSSL_VERSION}")
    message(STATUS "OpenSSL Include: ${OPENSSL_INCLUDE_DIR}")

    # Check for ARIA support in OpenSSL
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_INCLUDES ${OPENSSL_INCLUDE_DIR})
    set(CMAKE_REQUIRED_LIBRARIES ${OPENSSL_LIBRARIES})
    check_cxx_source_compiles("
        #include <openssl/evp.h>
        int main() {
            const EVP_CIPHER* cipher = EVP_aria_256_gcm();
            return cipher ? 0 : 1;
        }
    " OPENSSL_HAS_ARIA)

    if(OPENSSL_HAS_ARIA)
        message(STATUS "OpenSSL ARIA support: YES")
    else()
        message(WARNING "OpenSSL does not support ARIA. KCMVP encryption may be incomplete.")
    endif()
else()
    message(FATAL_ERROR "OpenSSL 3.0+ not found. Please install OpenSSL 3.0+")
endif()

# Find FFmpeg (for RTSP client and codec utilities)
find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(AVFORMAT libavformat)
    pkg_check_modules(AVCODEC libavcodec)
    pkg_check_modules(AVUTIL libavutil)

    if(AVFORMAT_FOUND AND AVCODEC_FOUND AND AVUTIL_FOUND)
        message(STATUS "FFmpeg libavformat: ${AVFORMAT_VERSION}")
        message(STATUS "FFmpeg libavcodec: ${AVCODEC_VERSION}")
        message(STATUS "FFmpeg libavutil: ${AVUTIL_VERSION}")
    else()
        message(WARNING "FFmpeg not found. RTSP client functionality may be limited.")
    endif()
endif()

# Find SQLite3 (for database)
find_package(SQLite3 REQUIRED)
if(SQLite3_FOUND)
    message(STATUS "SQLite3: ${SQLite3_VERSION}")
else()
    message(FATAL_ERROR "SQLite3 not found. Please install SQLite3 development package.")
endif()

# Platform-specific: KCMVP Crypto Libraries
if(UNIX)
    # Try to find KryptoAPI on Linux (KCMVP-certified LEA/ARIA)
    find_library(KRYPTOAPI_LIB NAMES kryptoapi libkryptoapi
                 PATHS /usr/lib /usr/local/lib)

    if(KRYPTOAPI_LIB)
        message(STATUS "KryptoAPI: Found (KCMVP-certified)")
        message(STATUS "KryptoAPI Library: ${KRYPTOAPI_LIB}")
        add_definitions(-DUSE_KRYPTOAPI)
        set(KCMVP_CRYPTO_BACKEND "KryptoAPI (KCMVP-certified)")
    else()
        message(WARNING "KryptoAPI not found. Using portable LEA implementation.")
        message(WARNING "For KCMVP certification, install KryptoAPI from https://seed.kisa.or.kr/")
        add_definitions(-DUSE_PORTABLE_LEA)
        set(KCMVP_CRYPTO_BACKEND "Portable LEA + OpenSSL ARIA")
    endif()

elseif(WIN32)
    # Windows always uses portable LEA + OpenSSL ARIA
    add_definitions(-DUSE_PORTABLE_LEA)
    message(STATUS "KCMVP Crypto: Portable LEA implementation + OpenSSL ARIA")
    set(KCMVP_CRYPTO_BACKEND "Portable LEA + OpenSSL ARIA")
endif()

message(STATUS "KCMVP Crypto Backend: ${KCMVP_CRYPTO_BACKEND}")

# Global include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CUDAToolkit_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${SQLite3_INCLUDE_DIRS}
)

if(NVDEC_FOUND)
    include_directories(${NVDEC_INCLUDE_DIR})
endif()

if(AVFORMAT_FOUND)
    include_directories(
        ${AVFORMAT_INCLUDE_DIRS}
        ${AVCODEC_INCLUDE_DIRS}
        ${AVUTIL_INCLUDE_DIRS}
    )
endif()

# Global link directories
link_directories(
    ${CUDAToolkit_LIBRARY_DIR}
)

if(AVFORMAT_FOUND)
    link_directories(
        ${AVFORMAT_LIBRARY_DIRS}
        ${AVCODEC_LIBRARY_DIRS}
        ${AVUTIL_LIBRARY_DIRS}
    )
endif()

# Subdirectories (ordered by dependency)
add_subdirectory(src/common)        # Common utilities and platform abstraction
add_subdirectory(src/core)          # Core engine (codec, network, GPU, threading, stream)
add_subdirectory(src/recording)     # Recording engine
add_subdirectory(src/server)        # Server component (multi-client support)
add_subdirectory(src/client)        # Client UI (Qt-based)

# Testing (optional)
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Tools (benchmarks, hardware detection)
add_subdirectory(tools)

# Installation rules
if(UNIX)
    set(CMAKE_INSTALL_PREFIX "/opt/fluxvision" CACHE PATH "Install prefix")

    install(TARGETS fluxvision-server fluxvision-client
            RUNTIME DESTINATION bin
            LIBRARY DESTINATION lib
            ARCHIVE DESTINATION lib)

    install(FILES config/config.yaml
            DESTINATION share/fluxvision/config)

    install(FILES scripts/fluxvision.service
            DESTINATION lib/systemd/system
            OPTIONAL)

elseif(WIN32)
    set(CMAKE_INSTALL_PREFIX "C:/Program Files/FluxVision VMS" CACHE PATH "Install prefix")

    install(TARGETS fluxvision-server fluxvision-client
            RUNTIME DESTINATION bin
            LIBRARY DESTINATION bin
            ARCHIVE DESTINATION lib)

    install(FILES config/config.yaml
            DESTINATION config)

    # Install Qt DLLs (Windows only)
    get_target_property(QT5_QMAKE_EXECUTABLE Qt5::qmake IMPORTED_LOCATION)
    get_filename_component(QT5_WINDEPLOYQT_EXECUTABLE ${QT5_QMAKE_EXECUTABLE} PATH)
    set(QT5_WINDEPLOYQT_EXECUTABLE "${QT5_WINDEPLOYQT_EXECUTABLE}/windeployqt.exe")

    install(CODE "execute_process(COMMAND ${QT5_WINDEPLOYQT_EXECUTABLE} \${CMAKE_INSTALL_PREFIX}/bin/fluxvision-client.exe)")
endif()

# Summary
message(STATUS "")
message(STATUS "===================================================")
message(STATUS "  Configuration Summary")
message(STATUS "===================================================")
message(STATUS "  Platform:          ${PLATFORM_NAME}")
message(STATUS "  C++ Compiler:      ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  CUDA:              ${CUDAToolkit_VERSION}")
message(STATUS "  Qt5:               ${Qt5_VERSION}")
message(STATUS "  OpenSSL:           ${OPENSSL_VERSION}")
message(STATUS "  SQLite3:           ${SQLite3_VERSION}")
if(AVFORMAT_FOUND)
message(STATUS "  FFmpeg:            ${AVFORMAT_VERSION}")
endif()
message(STATUS "  KCMVP Crypto:      ${KCMVP_CRYPTO_BACKEND}")
message(STATUS "  Build Type:        ${CMAKE_BUILD_TYPE}")
message(STATUS "  Install Prefix:    ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Build Tests:       ${BUILD_TESTS}")
message(STATUS "===================================================")
message(STATUS "")
