cmake_minimum_required(VERSION 3.15)

project(orsend VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /W3 /MP)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
    # 静态链接 CRT，产物可直接拷贝到其它机器运行
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    add_compile_options(-Wall -Wextra)
    set(CMAKE_CXX_FLAGS_RELEASE "-O2")
endif()

add_subdirectory(core)
add_subdirectory(apps/cli)

message(STATUS "orSend ${PROJECT_VERSION} 构建目录: ${CMAKE_BINARY_DIR}")
