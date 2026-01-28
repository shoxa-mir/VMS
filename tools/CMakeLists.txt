# tools/CMakeLists.txt

# Hardware detection tool (requires CUDA)
if(CUDAToolkit_FOUND)
    add_executable(hardware-detect hardware_detect.cpp)
    target_link_libraries(hardware-detect CUDA::cuda_driver CUDA::cudart)
else()
    message(STATUS "Skipping hardware-detect (requires CUDA)")
endif()

# Decoder test tool (Phase 1 validation)
add_executable(decoder-test decoder_test.cpp)
target_link_libraries(decoder-test
    ${CMAKE_PROJECT_NAME}-core
    ${CMAKE_PROJECT_NAME}-common
)
target_include_directories(decoder-test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
