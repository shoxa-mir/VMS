# tools/CMakeLists.txt
# Hardware detection tool (requires CUDA)
if(CUDAToolkit_FOUND)
    add_executable(hardware-detect hardware_detect.cpp)
    target_link_libraries(hardware-detect CUDA::cuda_driver CUDA::cudart)
else()
    message(STATUS "Skipping hardware-detect (requires CUDA)")
endif()
