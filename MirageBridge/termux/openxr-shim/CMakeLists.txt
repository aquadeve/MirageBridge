add_library(openxr_mirage SHARED
    openxr_shim.cpp
)

target_link_libraries(openxr_mirage
    miragebridge_common
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

target_include_directories(openxr_mirage PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../monado-referenceOnly/src/external/openxr_includes
)

if(UNIX)
    target_link_options(openxr_mirage PRIVATE -Wl,-Bsymbolic-functions)
endif()

file(GENERATE
    OUTPUT $<TARGET_FILE_DIR:openxr_mirage>/openxr_mirage_runtime.json
    CONTENT "{\n  \"file_format_version\": \"1.0.0\",\n  \"runtime\": {\n    \"name\": \"MirageBridge OpenXR\",\n    \"library_path\": \"$<TARGET_FILE:openxr_mirage>\"\n  }\n}\n"
)
