# src/server/CMakeLists.txt
add_executable(fluxvision-server main.cpp)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/main.cpp "int main() { return 0; }\n")
target_link_libraries(fluxvision-server fluxvision-core fluxvision-recording)
