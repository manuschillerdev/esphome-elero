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

# Include paths
include_directories(${CMAKE_SOURCE_DIR}/../../components)  # elero headers
include_directories(${CMAKE_SOURCE_DIR})                   # test_vectors.h

# Test executable: Protocol encoding/decoding
add_executable(test_protocol
  test_protocol.cpp
)
target_link_libraries(test_protocol GTest::gtest_main)

# Test executable: Real packet vectors
add_executable(test_packet_vectors
  test_packet_vectors.cpp
)
target_link_libraries(test_packet_vectors GTest::gtest_main)

# Discover all tests
include(GoogleTest)
gtest_discover_tests(test_protocol)
gtest_discover_tests(test_packet_vectors)

# Combined target for running all tests
add_custom_target(run_all_tests
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  DEPENDS test_protocol test_packet_vectors
)
