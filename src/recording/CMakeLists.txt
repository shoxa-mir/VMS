# src/recording/CMakeLists.txt
add_library(${CMAKE_PROJECT_NAME}-recording STATIC .placeholder.cpp)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/.placeholder.cpp "namespace ${CMAKE_PROJECT_NAME} { void recording_init() {} }\n")
target_include_directories(${CMAKE_PROJECT_NAME}-recording PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(${CMAKE_PROJECT_NAME}-recording PUBLIC ${CMAKE_PROJECT_NAME}-core SQLite::SQLite3)
