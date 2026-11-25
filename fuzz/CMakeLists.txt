# libFuzzer targets for the collector: a host build with clang (ASan +
# UBSan). nlohmann/json is only used here, to check that what goes on the
# socket is JSON. fuzz/run.sh builds and runs them.
cmake_minimum_required(VERSION 3.25)
project(aember-stats-fuzz LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "libFuzzer needs clang (CXX=clang++-18)")
endif()

set(SANITIZE -fsanitize=address,undefined -fno-sanitize-recover=all
             -fno-omit-frame-pointer)
add_compile_options(-g -O1 -Wall -Wextra ${SANITIZE} -fsanitize=fuzzer-no-link)
add_link_options(${SANITIZE})

find_package(nlohmann_json 3 REQUIRED)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
add_library(aember-stats-lib STATIC ${SRC}/collector.cpp)
target_include_directories(aember-stats-lib
                           PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)

foreach(target parsers json sample)
  add_executable(fuzz_${target} fuzz_${target}.cpp)
  target_link_libraries(fuzz_${target} PRIVATE aember-stats-lib
                                               nlohmann_json::nlohmann_json)
  target_link_options(fuzz_${target} PRIVATE -fsanitize=fuzzer)
endforeach()
