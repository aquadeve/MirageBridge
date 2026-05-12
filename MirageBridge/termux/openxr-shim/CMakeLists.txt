add_library(openxr_mirage SHARED
    openxr_shim.cpp
)

target_link_libraries(openxr_mirage
    miragebridge_common
    Threads::Threads
)

target_include_directories(openxr_mirage PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_custom_command(TARGET openxr_mirage POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/openxr_mirage_runtime.json
        $<TARGET_FILE_DIR:openxr_mirage>/openxr_mirage_runtime.json
)
