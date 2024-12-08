
add_subdirectory(inputs)

enable_testing()

add_executable(vec_test vec_test.cc)
target_link_libraries(vec_test mattc_vec GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(vec_test)
