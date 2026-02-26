cmake_minimum_required(VERSION 3.14)
project(elero_unit_tests)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# Include path for elero headers
include_directories(${CMAKE_SOURCE_DIR}/../../components)

# Test executable
add_executable(elero_tests
  test_protocol.cpp
)

target_link_libraries(elero_tests
  GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(elero_tests)
