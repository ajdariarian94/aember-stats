# GoogleTest, pinned by URL and hash (host builds only).
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
  URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(aember-stats-unittest test_collector.cpp test_server.cpp)
target_link_libraries(aember-stats-unittest PRIVATE aember-stats-lib GTest::gtest_main)
target_compile_definitions(aember-stats-unittest
  PRIVATE AEMBER_STATS_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
include(GoogleTest)
gtest_discover_tests(aember-stats-unittest)
