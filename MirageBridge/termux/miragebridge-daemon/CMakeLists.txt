add_executable(miragebridge-daemon
    main.cpp
)

target_link_libraries(miragebridge-daemon
    miragebridge_common
)

target_include_directories(miragebridge-daemon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
