cmake_minimum_required(VERSION 3.20)

project(MatrixMonitor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(MatrixMonitor
    src/main.cpp
    src/MatrixRain.cpp
    src/SystemMetrics.cpp
)

target_include_directories(MatrixMonitor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (MSVC)
    target_compile_options(MatrixMonitor PRIVATE /W4 /EHsc)
endif()

target_link_libraries(MatrixMonitor PRIVATE
    iphlpapi
    ws2_32
    dxgi
    winhttp
)
