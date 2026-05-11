add_executable(sbs-frame-client
    main.cpp
)

target_link_libraries(sbs-frame-client
    miragebridge_common
)

target_include_directories(sbs-frame-client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
