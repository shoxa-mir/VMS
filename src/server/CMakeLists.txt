# src/server/CMakeLists.txt
add_executable(${CMAKE_PROJECT_NAME}-server main.cpp)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/main.cpp "int main() { return 0; }\n")
target_link_libraries(${CMAKE_PROJECT_NAME}-server ${CMAKE_PROJECT_NAME}-core ${CMAKE_PROJECT_NAME}-recording)
