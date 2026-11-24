find_package(benchmark REQUIRED CONFIG)

add_executable(bench_main bench_main.cpp bench_compare.cpp)
target_link_libraries(bench_main PRIVATE pcrepp::pcrepp benchmark::benchmark_main)
target_compile_features(bench_main PRIVATE cxx_std_23)

if(WITH_CTRE)
  find_package(ctre CONFIG REQUIRED)
  target_link_libraries(bench_main PRIVATE ctre::ctre)
  target_compile_definitions(bench_main PRIVATE WITH_CTRE=1)
endif()
