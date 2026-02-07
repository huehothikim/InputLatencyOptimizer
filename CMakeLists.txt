cmake_minimum_required(VERSION 3.21)
project(InputLatencyOptimizer LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(InputLatencyOptimizer WIN32
    src/main.cpp
    src/InputThread.cpp
    src/LatencyMeasurer.cpp
    src/AutoStartManager.cpp
    src/SettingsDialog.cpp
    src/TrayIcon.cpp
    src/RingBuffer.cpp
    src/DeviceTuner.cpp
    src/ConfigStore.cpp
    assets/app.rc
)

target_include_directories(InputLatencyOptimizer PRIVATE include)

target_link_libraries(InputLatencyOptimizer PRIVATE
    avrt
    winmm
    comctl32
    psapi
    taskschd
    comsuppw
)

# Make sure Unicode is enabled
target_compile_definitions(InputLatencyOptimizer PRIVATE UNICODE _UNICODE)
