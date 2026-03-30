add_executable(hvast main.c)
target_link_libraries(hvast haven_compiler_options compiler_bootstrap)
set_target_properties(hvast PROPERTIES LINKER_LANGUAGE CXX)

