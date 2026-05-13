add_library(mirage_runtime_obj OBJECT
    src/mirage_runtime.cpp
)

target_compile_definitions(mirage_runtime_obj PRIVATE MIRAGE_RUNTIME_BUILD=1)
set_target_properties(mirage_runtime_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(mirage_runtime_obj PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)

add_library(mirage_runtime SHARED
    $<TARGET_OBJECTS:mirage_runtime_obj>
)

add_library(mirage_runtime_static STATIC
    $<TARGET_OBJECTS:mirage_runtime_obj>
)

set_target_properties(mirage_runtime_static PROPERTIES OUTPUT_NAME mirage_runtime)

target_link_libraries(mirage_runtime
    miragebridge_common
    Threads::Threads
)

target_link_libraries(mirage_runtime_static
    miragebridge_common
    Threads::Threads
)

target_include_directories(mirage_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/include
)

target_include_directories(mirage_runtime_static PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/include
)

add_library(mirage_luau MODULE
    luau/vr_luau.cpp
)

set_target_properties(mirage_luau PROPERTIES
    PREFIX ""
    OUTPUT_NAME vr
)

target_include_directories(mirage_luau PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/include
)

target_link_libraries(mirage_luau
    mirage_runtime
)

if(UNIX AND NOT APPLE)
    target_link_options(mirage_luau PRIVATE "-Wl,--unresolved-symbols=ignore-all")
endif()
