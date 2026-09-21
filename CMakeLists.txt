cmake_minimum_required(VERSION 3.20)
project(DisplaySwap LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# [수정] 이름 뒤에 WIN32를 추가하면 콘솔 창이 뜨지 않는 백그라운드 앱으로 빌드됩니다.
add_executable(DisplaySwapDaemon WIN32
    DisplaySwapDaemon/main.cpp
    DisplaySwapDaemon/DisplayManager.cpp
    DisplaySwapDaemon/DisplayManager.h
    DisplaySwapDaemon/NamedPipeServer.cpp
    DisplaySwapDaemon/NamedPipeServer.h
    DisplaySwapDaemon/MonitorWindow.cpp
    DisplaySwapDaemon/MonitorWindow.h
)

add_executable(DisplaySwap
    DisplaySwap/main.cpp
)

target_compile_definitions(DisplaySwapDaemon PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_definitions(DisplaySwap PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

target_link_libraries(DisplaySwapDaemon PRIVATE user32 advapi32)
target_link_libraries(DisplaySwap PRIVATE advapi32)