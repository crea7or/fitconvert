cmake_minimum_required(VERSION 3.15)

project("fitconvert")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(FITSDK_SRC
  "fitsdk/fit_convert.h"
  "fitsdk/fit_crc.c"
  "fitsdk/fit_crc.h"
  "fitsdk/fit_example.c"
  "fitsdk/fit_example.h"
  "fitsdk/fit_product.h"
  "fitsdk/fit_ram.c"
  "fitsdk/fit_ram.h"
  "fitsdk/fit.c"
  "fitsdk/fit.h"
  "fitsdk/fit_config.h"
  "fitsdk/fit_convert.c"
  )

set(TARGET_SRC
  "parser.cpp"
  "parser.h"
  "datasource.cpp"
  "datasource.h"
  )


# init for vs 2002
# conan install . -s build_type=Release --build=missing
# conan install . -s build_type=Debug --build=missing
# cmake -B build -S . -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE=build/generators/conan_toolchain.cmake
# Open build\fit2srt.sln

find_package(spdlog REQUIRED)
find_package(RapidJSON REQUIRED)
find_package(cxxopts REQUIRED)
find_package(GTest REQUIRED)
find_package(benchmark REQUIRED)

# main target
set(MAIN_SRC
  "main.cpp"
  )

add_executable(${PROJECT_NAME} ${MAIN_SRC} ${TARGET_SRC} ${FITSDK_SRC})
target_link_libraries(${PROJECT_NAME} PRIVATE fmt::fmt
        spdlog::spdlog
        cxxopts::cxxopts
        rapidjson
        )

# benchmark target
set(BENCH_PROJECT_NAME "fitconvert-bench")
set(BENCH_SRC
  "bench.cpp"
  )

add_executable(${BENCH_PROJECT_NAME} ${BENCH_SRC} ${TARGET_SRC} ${FITSDK_SRC})
target_link_libraries(${BENCH_PROJECT_NAME} PRIVATE fmt::fmt
        spdlog::spdlog
        cxxopts::cxxopts
        rapidjson
        benchmark::benchmark_main
        )

# tests target
set(TEST_PROJECT_NAME "fitconvert-tests")
set(TEST_SOURCES
  "tests.cpp"
  )

enable_testing()

add_executable(${TEST_PROJECT_NAME} ${TEST_SOURCES} ${FITSDK_SRC})
target_link_libraries(${TEST_PROJECT_NAME} PRIVATE fmt::fmt
        spdlog::spdlog
        cxxopts::cxxopts
        rapidjson
        gtest::gtest)

include(GoogleTest)

gtest_discover_tests(${TEST_PROJECT_NAME})
