# src/core/CMakeLists.txt
# Core engine (codec, network, GPU, threading, stream)

# Collect source files
set(CORE_SOURCES
    # GPU management
    gpu/cuda_context.cpp
)

# Collect header files (for IDE integration)
set(CORE_HEADERS
    codec/types.h
    codec/decoder_interface.h
    gpu/cuda_context.h
)

add_library(${CMAKE_PROJECT_NAME}-core STATIC
    ${CORE_SOURCES}
    ${CORE_HEADERS}
)

target_include_directories(${CMAKE_PROJECT_NAME}-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(${CMAKE_PROJECT_NAME}-core PUBLIC
    ${CMAKE_PROJECT_NAME}-common
)

# Link CUDA libraries only if CUDA is available
if(CUDAToolkit_FOUND)
    target_link_libraries(${CMAKE_PROJECT_NAME}-core PUBLIC
        CUDA::cuda_driver
        CUDA::cudart
    )
endif()

# Link NVDEC only if found
if(NVDEC_FOUND)
    target_link_libraries(${CMAKE_PROJECT_NAME}-core PUBLIC NVDEC::nvdec)
endif()
