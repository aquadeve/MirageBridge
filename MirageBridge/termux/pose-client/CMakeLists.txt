add_executable(pose-client
    main.cpp
)

target_link_libraries(pose-client
    miragebridge_common
)

target_include_directories(pose-client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
