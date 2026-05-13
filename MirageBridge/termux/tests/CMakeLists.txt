add_executable(mbr-protocol-smoke
    protocol_smoke.cpp
)

target_link_libraries(mbr-protocol-smoke
    miragebridge_common
)

add_test(NAME mbr-protocol-smoke COMMAND mbr-protocol-smoke)
