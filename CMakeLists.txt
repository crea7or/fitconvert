cmake_minimum_required(VERSION 3.31)

project("fitconvert")

set(CMAKE_CXX_STANDARD 20)
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

# conan install . -s build_type=Release --build=missing
# conan install . -s build_type=Debug --build=missing
# init for cmake >= 3.23
# cmake --preset conan-default
# init for cmake < 3.23
# vs2022
# cmake -B build -S . -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE=build/generators/conan_toolchain.cmake
# open build\fit2srt.sln
# other
# cmake <path> -G Ninja -DCMAKE_TOOLCHAIN_FILE=generators/conan_toolchain.cmake  -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_BUILD_TYPE=Release
# for Ubuntu you need clang or gcc with c++20 support (same for libc++) due to use of std::to_chars with 5th params
# build
# cmake --build build/Release --config Release

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
# bench.cpp includes parser.cpp to reach internal helpers, like the tests
set(BENCH_SRC
  "bench.cpp"
  "fit_writer.h"
  "datasource.cpp"
  )

add_executable(${BENCH_PROJECT_NAME} ${BENCH_SRC} ${FITSDK_SRC})
target_link_libraries(${BENCH_PROJECT_NAME} PRIVATE fmt::fmt
        spdlog::spdlog
        cxxopts::cxxopts
        rapidjson
        benchmark::benchmark
        )

# tests target
set(TEST_PROJECT_NAME "fitconvert-tests")
set(TEST_SOURCES
  "tests.cpp"
  "tests_convert.cpp"
  "tests_datasource.cpp"
  "fit_writer.h"
  "datasource.cpp"
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

# coverage of the tests target
# clang/clang-cl: source based coverage, run tests with LLVM_PROFILE_FILE set, then llvm-profdata merge + llvm-cov report
# gcc: gcov
option(FITCONVERT_COVERAGE "Instrument tests for code coverage" OFF)
if(FITCONVERT_COVERAGE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(${TEST_PROJECT_NAME} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
      # clang-cl links with link.exe/lld-link, the profile runtime has to be added explicitly
      execute_process(COMMAND ${CMAKE_CXX_COMPILER} /clang:-print-resource-dir
                      OUTPUT_VARIABLE CLANG_RESOURCE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE)
      find_library(CLANG_PROFILE_RUNTIME NAMES clang_rt.profile-x86_64 clang_rt.profile
                   HINTS "${CLANG_RESOURCE_DIR}/lib/windows" "${CLANG_RESOURCE_DIR}/lib/x86_64-pc-windows-msvc" REQUIRED)
      target_link_libraries(${TEST_PROJECT_NAME} PRIVATE ${CLANG_PROFILE_RUNTIME})
    else()
      target_link_options(${TEST_PROJECT_NAME} PRIVATE -fprofile-instr-generate)
    endif()
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(${TEST_PROJECT_NAME} PRIVATE --coverage -O0)
    target_link_options(${TEST_PROJECT_NAME} PRIVATE --coverage)
  else()
    message(FATAL_ERROR "FITCONVERT_COVERAGE requires clang or gcc")
  endif()
endif()
