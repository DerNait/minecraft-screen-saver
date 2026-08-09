cmake_minimum_required(VERSION 3.20)
project(MinecraftScreenSaver CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(GLEW REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(OpenMP REQUIRED)

add_executable(secuencial Secuencial/secuencial.cpp)
target_link_libraries(secuencial PRIVATE glfw GLEW::GLEW OpenGL::GL glm::glm OpenMP::OpenMP_CXX)

add_executable(paralelo Paralelo/paralelo.cpp)
target_link_libraries(paralelo PRIVATE glfw GLEW::GLEW OpenGL::GL glm::glm OpenMP::OpenMP_CXX)
