# orSend 核心库：协议 + 网络 + 转发/映射/隧道
add_library(orsend_core STATIC
    common/Common.cpp
    common/Json.cpp
    common/Logger.cpp

    net/NetCompat.cpp
    net/EventLoop.cpp
    net/TcpConnection.cpp
    net/TcpListener.cpp
    net/ConnectionReaper.cpp

    protocol/Protocol.cpp

    relay/RelayEngine.cpp
    forward/PortForward.cpp
    mapping/PortMapping.cpp

    tunnel/TunnelServer.cpp
    tunnel/TunnelClient.cpp

    control/ControlServer.cpp
    app/OrSendApp.cpp
)

target_include_directories(orsend_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/net
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/relay
    ${CMAKE_CURRENT_SOURCE_DIR}/forward
    ${CMAKE_CURRENT_SOURCE_DIR}/mapping
    ${CMAKE_CURRENT_SOURCE_DIR}/tunnel
    ${CMAKE_CURRENT_SOURCE_DIR}/control
    ${CMAKE_CURRENT_SOURCE_DIR}/app
)

if(WIN32)
    target_link_libraries(orsend_core PUBLIC ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(orsend_core PUBLIC Threads::Threads)
endif()
