# CMakeList.txt: orSend 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#
cmake_minimum_required (VERSION 3.8)


set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(_UTF8_SOURCE)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # 编译阶段添加 -pthread（启用线程宏，兼容 std::thread 底层）
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
    # 链接阶段添加 -pthread（链接 pthread 库）
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread")
    # 查找 pthread 线程库（Linux 下必需）
    find_package(Threads REQUIRED)
endif()
# 将源代码添加到此项目的可执行文件。
add_executable (orSend "orSend.cpp" "orSend.h" "core/TcpSocketClass.h" "core/TcpSocketClass.cpp" "core/PortForward.h" "core/PortForward.cpp" "core/PortMapping.h" "core/PortMapping.cpp" "core/ThreadPool.h" "core/ThreadPool.cpp")



# TODO: 如有需要，请添加测试并安装目标。
