# src/recording/CMakeLists.txt
add_library(fluxvision-recording STATIC .placeholder.cpp)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/.placeholder.cpp "namespace FluxVision { void recording_init() {} }\n")
target_include_directories(fluxvision-recording PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fluxvision-recording PUBLIC fluxvision-core SQLite::SQLite3)
