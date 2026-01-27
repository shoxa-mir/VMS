# FindNVDEC.cmake
# Finds NVIDIA Video Codec SDK (NVDEC/NVENC)
#
# Variables set by this module:
#   NVDEC_FOUND         - True if NVDEC SDK found
#   NVDEC_INCLUDE_DIR   - Include directory path
#   NVDEC_VERSION       - SDK version (if available)
#
# Download NVIDIA Video Codec SDK from:
# https://developer.nvidia.com/nvidia-video-codec-sdk

# Search paths for NVDEC SDK
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

# Find include directory
find_path(NVDEC_INCLUDE_DIR
    NAMES nvcuvid.h cuviddec.h
    PATHS ${NVDEC_SEARCH_PATHS}
    PATH_SUFFIXES include Interface
    DOC "NVDEC include directory"
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
    REQUIRED_VARS NVDEC_INCLUDE_DIR
    VERSION_VAR NVDEC_VERSION
)

# Mark cached variables as advanced
mark_as_advanced(
    NVDEC_INCLUDE_DIR
    NVDEC_VERSION
)

# Create imported target
if(NVDEC_FOUND AND NOT TARGET NVDEC::nvdec)
    add_library(NVDEC::nvdec INTERFACE IMPORTED)
    set_target_properties(NVDEC::nvdec PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${NVDEC_INCLUDE_DIR}"
    )
endif()

# Informational message
if(NVDEC_FOUND)
    message(STATUS "Found NVDEC SDK: ${NVDEC_INCLUDE_DIR} (version ${NVDEC_VERSION})")
else()
    message(STATUS "NVDEC SDK not found. Set NVDEC_SDK_PATH environment variable or download from:")
    message(STATUS "  https://developer.nvidia.com/nvidia-video-codec-sdk")
endif()
