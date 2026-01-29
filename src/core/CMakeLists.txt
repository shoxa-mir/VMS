# src/core/CMakeLists.txt
# Core engine (codec, network, GPU, threading, stream)

# Collect source files
set(CORE_SOURCES
    # GPU management
    gpu/cuda_context.cpp
    gpu/memory_pool.cpp

    # Codec/Decoder implementations
    codec/nvdec_decoder.cpp
    codec/cpu_decoder.cpp
    codec/decoder_factory.cpp

    # Network layer (Phase 2)
    network/rtsp_client.cpp
    network/rtp_depacketizer.cpp
    network/h264_parser.cpp
    network/bitstream_parser.cpp

    # Threading layer (Phase 3)
    threading/thread_pool.cpp
    threading/network_thread_pool.cpp
    threading/decode_thread_pool.cpp

    # Stream management (Phase 3)
    stream/camera_stream.cpp
    stream/stream_manager.cpp
    stream/pipeline.cpp
)

# Collect header files (for IDE integration)
set(CORE_HEADERS
    codec/types.h
    codec/decoder_interface.h
    codec/nvdec_decoder.h
    codec/cpu_decoder.h
    codec/decoder_factory.h
    gpu/cuda_context.h
    gpu/memory_pool.h

    # Network headers
    network/types.h
    network/rtsp_client.h
    network/rtp_depacketizer.h
    network/h264_parser.h
    network/bitstream_parser.h

    # Threading headers (Phase 3)
    threading/thread_pool.h
    threading/bounded_queue.h
    threading/network_thread_pool.h
    threading/decode_thread_pool.h

    # Stream management headers (Phase 3)
    stream/camera_stream.h
    stream/stream_manager.h
    stream/pipeline.h
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
    target_include_directories(${CMAKE_PROJECT_NAME}-core PUBLIC ${NVDEC_INCLUDE_DIR})
endif()

# Link FFmpeg libraries (required for CPU decoder and RTSP client)
if(AVCODEC_FOUND AND AVUTIL_FOUND)
    target_link_libraries(${CMAKE_PROJECT_NAME}-core PUBLIC
        ${AVCODEC_LIBRARIES}
        ${AVUTIL_LIBRARIES}
    )
    target_include_directories(${CMAKE_PROJECT_NAME}-core PUBLIC
        ${AVCODEC_INCLUDE_DIRS}
        ${AVUTIL_INCLUDE_DIRS}
    )
endif()

# Link libavformat for RTSP/RTP (Phase 2: Network layer)
if(AVFORMAT_FOUND)
    target_link_libraries(${CMAKE_PROJECT_NAME}-core PUBLIC
        ${AVFORMAT_LIBRARIES}
    )
    target_include_directories(${CMAKE_PROJECT_NAME}-core PUBLIC
        ${AVFORMAT_INCLUDE_DIRS}
    )
endif()
