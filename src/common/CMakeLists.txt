# src/common/CMakeLists.txt
# Common utilities and platform abstraction layer

add_library(${CMAKE_PROJECT_NAME}-common STATIC
    # Platform abstraction (will be added in Phase 0)
    # platform/filesystem.cpp
    # platform/linux/daemon.cpp (Linux only)
    # platform/windows/service.cpp (Windows only)

    # Crypto interface (will be added with KCMVP implementation)
    # crypto/kcmvp_interface.cpp
    # crypto/lea/lea_portable.cpp
    # crypto/linux/kcmvp_linux.cpp (Linux only)
    # crypto/windows/kcmvp_windows.cpp (Windows only)

    # Placeholder to make library buildable
    .placeholder.cpp
)

# Create placeholder file
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/.placeholder.cpp
    "// Placeholder file - will be replaced with actual implementation\n"
    "namespace ${CMAKE_PROJECT_NAME} { void common_init() {} }\n"
)

target_include_directories(${CMAKE_PROJECT_NAME}-common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(${CMAKE_PROJECT_NAME}-common PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
)

# Platform-specific libraries
if(UNIX)
    target_link_libraries(${CMAKE_PROJECT_NAME}-common PUBLIC pthread dl)
    if(KRYPTOAPI_LIB)
        target_link_libraries(${CMAKE_PROJECT_NAME}-common PUBLIC ${KRYPTOAPI_LIB})
    endif()
elseif(WIN32)
    target_link_libraries(${CMAKE_PROJECT_NAME}-common PUBLIC ws2_32 advapi32)
endif()
