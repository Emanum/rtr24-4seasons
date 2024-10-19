add_executable(fourSeasons
    source/fourSeasons.cpp)
target_include_directories(fourSeasons PRIVATE ${PROJECT_NAME})
target_link_libraries(fourSeasons PRIVATE ${PROJECT_NAME})

get_target_property(fourSeasons_BINARY_DIR fourSeasons BINARY_DIR)

add_post_build_commands(fourSeasons
    ${PROJECT_SOURCE_DIR}/examples/fourSeasons/shaders
    ${fourSeasons_BINARY_DIR}/shaders
    $<TARGET_FILE_DIR:fourSeasons>/assets
    ""
    ${avk_toolkit_CreateDependencySymlinks})