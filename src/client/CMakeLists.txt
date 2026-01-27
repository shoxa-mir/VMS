# src/client/CMakeLists.txt
add_executable(${CMAKE_PROJECT_NAME}-client main.cpp)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/main.cpp "int main() { return 0; }\n")
target_link_libraries(${CMAKE_PROJECT_NAME}-client ${CMAKE_PROJECT_NAME}-core Qt5::Core Qt5::Widgets Qt5::OpenGL)
