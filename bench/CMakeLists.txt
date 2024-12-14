
add_executable(bench_vec bench_vec.cc)
target_link_libraries(bench_vec cmake_base_compiler_options benchmark::benchmark mattc_vec)

add_executable(bench_kw bench_kw.cc)
target_link_libraries(bench_kw cmake_base_compiler_options benchmark::benchmark compiler)

