add_library(openxr_mirage SHARED
    openxr_shim.cpp
)

target_link_libraries(openxr_mirage
    miragebridge_common
)

target_include_directories(openxr_mirage PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
