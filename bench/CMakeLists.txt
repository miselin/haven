
if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # vec/mat benchmarks use the Clang vector/matrix extensions
    add_executable(bench_vec bench_vec.cc)
    target_link_libraries(bench_vec haven_compiler_options_no_warnings benchmark::benchmark haven_vec)

    add_executable(bench_mat bench_mat.cc)
    target_link_libraries(bench_mat haven_compiler_options_no_warnings benchmark::benchmark haven_mat)
endif ()

add_executable(bench_kw bench_kw.cc)
target_link_libraries(bench_kw haven_compiler_options_no_warnings benchmark::benchmark compiler)

