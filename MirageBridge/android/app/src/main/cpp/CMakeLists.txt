cmake_minimum_required(VERSION 3.22)
project(miragebridge LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(miragebridge SHARED
    jni_bridge.cpp
    core/mirage_bridge_core.cpp
    core/gvr_tracking_adapter.cpp
    core/shared_memory_transport.cpp
    core/stereo_renderer.cpp
    core/egl_capture_pipeline.cpp
    core/frame_timing_diagnostics.cpp
    core/unix_socket_transport.cpp
)

target_include_directories(miragebridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../common
)

if(DEFINED GVR_SDK_ROOT)
    target_include_directories(miragebridge PRIVATE
        ${GVR_SDK_ROOT}/libraries/headers
        ${GVR_SDK_ROOT}/samples/ndk-hellovr/src/main/jni
    )
    target_compile_definitions(miragebridge PRIVATE MIRAGEBRIDGE_ENABLE_GVR=1)
endif()

find_library(log-lib log)
find_library(android-lib android)
find_library(egl-lib EGL)
find_library(gles-lib GLESv3)

target_link_libraries(miragebridge
    ${log-lib}
    ${android-lib}
    ${egl-lib}
    ${gles-lib}
)
