# src/core/CMakeLists.txt
# Core engine (codec, network, GPU, threading, stream)

add_library(fluxvision-core STATIC
    .placeholder.cpp
)

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/.placeholder.cpp
    "// Placeholder file\nnamespace FluxVision { void core_init() {} }\n"
)

target_include_directories(fluxvision-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(fluxvision-core PUBLIC
    fluxvision-common
    CUDA::cuda_driver
    CUDA::cudart
)

if(NVDEC_FOUND)
    target_link_libraries(fluxvision-core PUBLIC NVDEC::nvdec)
endif()
