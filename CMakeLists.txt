cmake_minimum_required(VERSION 3.25)

project(WinIconManagement LANGUAGES CXX RC)

set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(WinIconManagement WIN32
    src/main.cpp
    src/App/AppController.cpp
    src/App/SingleInstance.cpp
    src/Desktop/DesktopWindowResolver.cpp
    src/Tray/TrayIcon.cpp
    src/Infrastructure/Dpi.cpp
    src/Infrastructure/Logger.cpp
    src/Infrastructure/Paths.cpp
    resources/app.rc
)

target_include_directories(WinIconManagement PRIVATE
    src
    resources
)

target_compile_definitions(WinIconManagement PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(WinIconManagement PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(WinIconManagement PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_link_libraries(WinIconManagement PRIVATE
    user32
    shell32
    shlwapi
)
