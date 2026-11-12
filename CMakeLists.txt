cmake_minimum_required(VERSION 3.20)
project(WAProxy VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    enable_language(RC)
endif()

find_package(OpenSSL 3.0 REQUIRED COMPONENTS SSL Crypto)
find_package(Threads REQUIRED)

add_library(waproxy_core STATIC
    client/Config.cpp
    client/Logger.cpp
    client/ConnectionManager.cpp
    client/ProxyServer.cpp
    client/RemoteTls.cpp
    common/Socket.cpp
    common/TlsRelay.cpp
)
target_include_directories(waproxy_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/client"
    "${CMAKE_CURRENT_SOURCE_DIR}/common"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(waproxy_core PUBLIC OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
if(WIN32)
    target_compile_definitions(waproxy_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
    target_link_libraries(waproxy_core PUBLIC ws2_32)
    if(MSVC)
        target_compile_options(waproxy_core PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()
else()
    target_compile_options(waproxy_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(WAProxy client/main.cpp)
target_link_libraries(WAProxy PRIVATE waproxy_core)

if(WIN32)
    add_executable(WAProxyGUI WIN32 client/gui_main.cpp resources/WAProxy.rc)
    target_link_libraries(WAProxyGUI PRIVATE waproxy_core user32 gdi32)
    if(MSVC)
        target_compile_options(WAProxyGUI PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()
endif()

if(UNIX AND NOT APPLE)
    add_executable(WAPServer
        server/main.cpp
        server/server.cpp
        common/Socket.cpp
        common/TlsRelay.cpp
    )
    target_include_directories(WAPServer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/common")
    target_link_libraries(WAPServer PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
    target_compile_options(WAPServer PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

    add_executable(WAProxyRelayTest EXCLUDE_FROM_ALL
        tests/tls_relay_test.cpp
        common/Socket.cpp
        common/TlsRelay.cpp
    )
    target_include_directories(WAProxyRelayTest PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/common")
    target_link_libraries(WAProxyRelayTest PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
    target_compile_options(WAProxyRelayTest PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_custom_command(TARGET WAProxy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:WAProxy>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config/client.example.json"
            "$<TARGET_FILE_DIR:WAProxy>/config/client.example.json"
)
if(TARGET WAPServer)
    add_custom_command(TARGET WAPServer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:WAPServer>/config"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/config/server.example.json"
                "$<TARGET_FILE_DIR:WAPServer>/config/server.example.json"
    )
endif()

install(TARGETS WAProxy RUNTIME DESTINATION .)
if(TARGET WAProxyGUI)
    install(TARGETS WAProxyGUI RUNTIME DESTINATION .)
endif()
if(TARGET WAPServer)
    install(TARGETS WAPServer RUNTIME DESTINATION .)
endif()
install(DIRECTORY config/ DESTINATION config FILES_MATCHING PATTERN "*.example.json")
