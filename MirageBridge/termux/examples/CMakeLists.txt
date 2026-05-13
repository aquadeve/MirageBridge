add_executable(mbr-pose-viewer
    c/pose_viewer.c
)

target_link_libraries(mbr-pose-viewer
    mirage_runtime
)

add_executable(mbr-submit-sbs
    cpp/submit_sbs.cpp
)

target_link_libraries(mbr-submit-sbs
    mirage_runtime
)
