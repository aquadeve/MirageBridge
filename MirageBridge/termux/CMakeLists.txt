cmake_minimum_required(VERSION 3.20)
project(miragebridge_termux LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_subdirectory(common)
add_subdirectory(sdk)
add_subdirectory(miragebridge-daemon)
add_subdirectory(pose-client)
add_subdirectory(sbs-frame-client)
add_subdirectory(openxr-shim)
add_subdirectory(examples)
enable_testing()
add_subdirectory(tests)
