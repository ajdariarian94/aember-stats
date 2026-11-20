cmake_minimum_required(VERSION 3.25)
project(aember-stats VERSION 0.1.0 LANGUAGES CXX)

# Cross builds: the aember-resources SDK's toolchain (`aember build -a <arch>`
# passes OEToolchainConfig.cmake). Host builds run the unit tests.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTS "Build the unit tests (host builds)" OFF)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

add_library(aember-stats-lib STATIC src/collector.cpp src/server.cpp)
target_include_directories(aember-stats-lib PUBLIC include)

add_executable(aember-stats src/main.cpp)
target_link_libraries(aember-stats PRIVATE aember-stats-lib)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  target_link_options(aember-stats PRIVATE -s)
endif()

include(GNUInstallDirs)
install(TARGETS aember-stats RUNTIME DESTINATION usr/bin)

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
