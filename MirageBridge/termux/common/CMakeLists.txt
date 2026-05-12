add_library(miragebridge_common STATIC
    transport_reader.cpp
    transport_writer.cpp
)

set_target_properties(miragebridge_common PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(miragebridge_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../common
)
