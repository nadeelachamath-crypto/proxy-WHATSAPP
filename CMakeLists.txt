cmake_minimum_required(VERSION 3.20)
project(WAProxy VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(WAProxy
    client/main.cpp
    client/Config.cpp
    client/Logger.cpp
    client/ConnectionManager.cpp
    client/ProxyServer.cpp
)

target_include_directories(WAProxy PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/client")

if(WIN32)
    target_compile_definitions(WAProxy PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601)
    target_link_libraries(WAProxy PRIVATE ws2_32)
    if(MSVC)
        target_compile_options(WAProxy PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()
else()
    find_package(Threads REQUIRED)
    target_link_libraries(WAProxy PRIVATE Threads::Threads)
    target_compile_options(WAProxy PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

# Make the example configuration available when running from the build directory.
add_custom_command(TARGET WAProxy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:WAProxy>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config/config.example.json"
            "$<TARGET_FILE_DIR:WAProxy>/config/config.example.json"
)
