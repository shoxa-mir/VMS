# src/core/CMakeLists.txt
# Core engine (codec, network, GPU, threading, stream)

add_library(${CMAKE_PROJECT_NAME}-core STATIC
    .placeholder.cpp
)

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/.placeholder.cpp
    "// Placeholder file\nnamespace ${CMAKE_PROJECT_NAME} { void core_init() {} }\n"
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
