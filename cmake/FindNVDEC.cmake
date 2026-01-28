# FindNVDEC.cmake
# Finds NVIDIA Video Codec SDK (NVDEC/NVENC)
#
# Variables set by this module:
#   NVDEC_FOUND         - True if NVDEC SDK found
#   NVDEC_INCLUDE_DIR   - Include directory path
#   NVDEC_LIBRARY       - NVDEC library (libnvcuvid)
#   NVDEC_VERSION       - SDK version (if available)
#
# Download NVIDIA Video Codec SDK from:
# https://developer.nvidia.com/nvidia-video-codec-sdk

# Search paths for NVDEC SDK headers
set(NVDEC_SEARCH_PATHS
    # User-specified environment variable
    $ENV{NVDEC_SDK_PATH}
    $ENV{VIDEO_CODEC_SDK_PATH}

    # Linux common paths
    /usr/local/cuda/include
    /opt/nvidia/video-codec-sdk
    /usr/local/video-codec-sdk

    # Windows common paths
    "C:/Program Files/NVIDIA GPU Computing Toolkit/Video_Codec_SDK"
    "C:/Program Files/NVIDIA Corporation/Video_Codec_SDK"
    "C:/NVIDIA/Video_Codec_SDK"
)

# Search paths for NVDEC library (comes with NVIDIA driver or SDK)
set(NVDEC_LIB_SEARCH_PATHS
    # Linux - comes with NVIDIA driver
    /usr/lib/x86_64-linux-gnu
    /usr/lib64
    /usr/lib
    /usr/local/lib
    /usr/local/cuda/lib64
    /usr/local/cuda/lib
    /usr/local/cuda-12.4/lib64
    /usr/local/cuda-12/lib64

    # Windows - NVDEC SDK includes stub libraries
    "$ENV{NVDEC_SDK_PATH}/Lib/x64"
    "$ENV{VIDEO_CODEC_SDK_PATH}/Lib/x64"
    "C:/Program Files/NVIDIA GPU Computing Toolkit/Video_Codec_SDK/Lib/x64"
    "C:/Program Files/NVIDIA Corporation/Video_Codec_SDK/Lib/x64"
    "C:/NVIDIA/Video_Codec_SDK/Lib/x64"

    # Windows - CUDA toolkit (older versions)
    "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.4/lib/x64"
    "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.0/lib/x64"
    "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v11.8/lib/x64"
)

# Find include directory
find_path(NVDEC_INCLUDE_DIR
    NAMES nvcuvid.h cuviddec.h
    PATHS ${NVDEC_SEARCH_PATHS}
    PATH_SUFFIXES include Interface
    DOC "NVDEC include directory"
)

# Find nvcuvid library
find_library(NVDEC_LIBRARY
    NAMES nvcuvid
    PATHS ${NVDEC_LIB_SEARCH_PATHS}
    DOC "NVDEC library (libnvcuvid)"
)

# Extract version from SDK header if available
if(NVDEC_INCLUDE_DIR AND EXISTS "${NVDEC_INCLUDE_DIR}/nvcuvid.h")
    file(READ "${NVDEC_INCLUDE_DIR}/nvcuvid.h" NVDEC_HEADER_CONTENTS)

    # Try to extract version (format: #define NVDECODE_API_VERSION VER_MAJOR * 1000 + VER_MINOR)
    string(REGEX MATCH "#define NVDECODE_API_MAJOR_VERSION ([0-9]+)" _ "${NVDEC_HEADER_CONTENTS}")
    set(NVDEC_VERSION_MAJOR "${CMAKE_MATCH_1}")

    string(REGEX MATCH "#define NVDECODE_API_MINOR_VERSION ([0-9]+)" _ "${NVDEC_HEADER_CONTENTS}")
    set(NVDEC_VERSION_MINOR "${CMAKE_MATCH_1}")

    if(NVDEC_VERSION_MAJOR AND NVDEC_VERSION_MINOR)
        set(NVDEC_VERSION "${NVDEC_VERSION_MAJOR}.${NVDEC_VERSION_MINOR}")
    else()
        # Try alternative version format
        string(REGEX MATCH "Video Codec SDK ([0-9]+\\.[0-9]+\\.[0-9]+)" _ "${NVDEC_HEADER_CONTENTS}")
        set(NVDEC_VERSION "${CMAKE_MATCH_1}")
    endif()

    if(NOT NVDEC_VERSION)
        set(NVDEC_VERSION "Unknown")
    endif()
endif()

# Handle find_package() arguments
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(NVDEC
    FOUND_VAR NVDEC_FOUND
    REQUIRED_VARS NVDEC_INCLUDE_DIR NVDEC_LIBRARY
    VERSION_VAR NVDEC_VERSION
)

# Mark cached variables as advanced
mark_as_advanced(
    NVDEC_INCLUDE_DIR
    NVDEC_LIBRARY
    NVDEC_VERSION
)

# Create imported target
if(NVDEC_FOUND AND NOT TARGET NVDEC::nvdec)
    add_library(NVDEC::nvdec UNKNOWN IMPORTED)
    set_target_properties(NVDEC::nvdec PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${NVDEC_INCLUDE_DIR}"
        IMPORTED_LOCATION "${NVDEC_LIBRARY}"
    )
endif()

# Informational message
if(NVDEC_FOUND)
    message(STATUS "Found NVDEC SDK: ${NVDEC_INCLUDE_DIR} (version ${NVDEC_VERSION})")
    message(STATUS "Found NVDEC Library: ${NVDEC_LIBRARY}")
else()
    if(NOT NVDEC_INCLUDE_DIR)
        message(STATUS "NVDEC SDK headers not found. Set NVDEC_SDK_PATH environment variable or download from:")
        message(STATUS "  https://developer.nvidia.com/nvidia-video-codec-sdk")
    endif()
    if(NOT NVDEC_LIBRARY)
        message(STATUS "NVDEC library (libnvcuvid) not found. Ensure NVIDIA driver is installed.")
    endif()
endif()
