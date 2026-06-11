add_executable(bench_main bench_main.cpp)
target_link_libraries(bench_main PRIVATE pcrepp::pcrepp)
target_compile_features(bench_main PRIVATE ${STD_CPP})
