cmake_minimum_required(VERSION 3.20)

project(epaper_spectra6_133_host_tests LANGUAGES CXX)

include(CTest)

option(ENABLE_COVERAGE "Build host tests with coverage instrumentation" OFF)
option(ENABLE_BENCHMARKS "Build host benchmarks with Google Benchmark" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(enable_coverage_if_requested target_name)
  if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target_name} PRIVATE -O0 -g --coverage)
    target_link_options(${target_name} PRIVATE --coverage)
  endif()
endfunction()

if(BUILD_TESTING OR ENABLE_BENCHMARKS)
  include(FetchContent)
endif()

if(BUILD_TESTING)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
  )
  FetchContent_MakeAvailable(googletest)

  add_executable(
    epaper_spectra6_133_framebuffer_tests
    framebuffer_test.cpp
    ../components/epaper_spectra6_133/epaper_spectra6_133_framebuffer.cpp
  )

  target_include_directories(
    epaper_spectra6_133_framebuffer_tests
    PRIVATE
      support
      ../components/epaper_spectra6_133
  )

  target_link_libraries(epaper_spectra6_133_framebuffer_tests PRIVATE GTest::gtest_main)
  enable_coverage_if_requested(epaper_spectra6_133_framebuffer_tests)

  add_executable(
    epaper_spectra6_133_controller_tests
    controller_test.cpp
    support/transport_stub.cpp
    ../components/epaper_spectra6_133/epaper_spectra6_133_controller.cpp
  )

  target_include_directories(
    epaper_spectra6_133_controller_tests
    PRIVATE
      support
      ../components/epaper_spectra6_133
  )

  target_link_libraries(epaper_spectra6_133_controller_tests PRIVATE GTest::gtest_main)
  enable_coverage_if_requested(epaper_spectra6_133_controller_tests)

  add_executable(
    epaper_spectra6_133_component_tests
    component_test.cpp
    support/transport_stub.cpp
    ../components/epaper_spectra6_133/epaper_spectra6_133_controller.cpp
    ../components/epaper_spectra6_133/epaper_spectra6_133_framebuffer.cpp
    ../components/epaper_spectra6_133/epaper_spectra6_133_operation.cpp
    ../components/epaper_spectra6_133/epaper_spectra6_133.cpp
  )

  target_include_directories(
    epaper_spectra6_133_component_tests
    PRIVATE
      support
      ../components/epaper_spectra6_133
  )

  target_link_libraries(epaper_spectra6_133_component_tests PRIVATE GTest::gtest_main)
  target_compile_definitions(
    epaper_spectra6_133_component_tests
    PRIVATE
      USE_LOGGER
      USE_LOGGER_RUNTIME_TAG_LEVELS
      ESPHOME_LOG_LEVEL=6
  )
  enable_coverage_if_requested(epaper_spectra6_133_component_tests)

  include(GoogleTest)
  gtest_discover_tests(epaper_spectra6_133_framebuffer_tests)
  gtest_discover_tests(epaper_spectra6_133_controller_tests)
  gtest_discover_tests(epaper_spectra6_133_component_tests)
endif()

if(ENABLE_BENCHMARKS)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/CodSpeedHQ/codspeed-cpp
    SOURCE_SUBDIR google_benchmark
    GIT_TAG main
  )
  FetchContent_MakeAvailable(googlebenchmark)

  add_executable(
    epaper_spectra6_133_benchmarks
    benchmarks/framebuffer_benchmark.cpp
    support/transport_stub.cpp
    ../components/epaper_spectra6_133/epaper_spectra6_133_controller.cpp
    ../components/epaper_spectra6_133/epaper_spectra6_133_framebuffer.cpp
    ../components/epaper_spectra6_133/epaper_spectra6_133_operation.cpp
    ../components/epaper_spectra6_133/epaper_spectra6_133.cpp
  )

  target_include_directories(
    epaper_spectra6_133_benchmarks
    PRIVATE
      support
      ../components/epaper_spectra6_133
  )

  target_link_libraries(epaper_spectra6_133_benchmarks PRIVATE benchmark::benchmark_main)
endif()
