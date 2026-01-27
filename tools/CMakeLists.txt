# tools/CMakeLists.txt
add_executable(hardware-detect hardware_detect.cpp)
target_link_libraries(hardware-detect CUDA::cuda_driver CUDA::cudart)
