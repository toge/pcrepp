find_package(Catch2 REQUIRED CONFIG)
find_package(frozenchars CONFIG)   # REQUIRED を外す — CI 環境では不要

file(GLOB test_src test_*.cpp)
add_executable(all_test ${test_src})

if(frozenchars_FOUND)
  target_link_libraries(all_test PRIVATE pcrepp Catch2::Catch2WithMain frozenchars::frozenchars)
  target_compile_definitions(all_test PRIVATE PCREPP_TEST_HAS_FROZENCHARS=1)
else()
  target_link_libraries(all_test PRIVATE pcrepp Catch2::Catch2WithMain)
  target_compile_definitions(all_test PRIVATE PCREPP_TEST_HAS_FROZENCHARS=0)
endif()

target_include_directories(all_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(all_test PRIVATE cxx_std_23)
add_test(NAME all_test COMMAND all_test)
