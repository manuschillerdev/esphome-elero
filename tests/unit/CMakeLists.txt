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
set(COMPONENTS_DIR ${CMAKE_SOURCE_DIR}/../../components)
include_directories(${COMPONENTS_DIR})          # elero headers
include_directories(${CMAKE_SOURCE_DIR})        # test_vectors.h

# Source files from components
set(ELERO_PACKET_SRC ${COMPONENTS_DIR}/elero/elero_packet.cpp)
set(ELERO_STRINGS_SRC ${COMPONENTS_DIR}/elero/elero_strings.cpp)
set(ELERO_TIME_PROVIDER_SRC ${COMPONENTS_DIR}/elero/time_provider.cpp)

# Test executable: Protocol encoding/decoding
add_executable(test_protocol
  test_protocol.cpp
)
target_link_libraries(test_protocol GTest::gtest_main)

# Test executable: Real packet vectors (uses elero_packet.cpp)
add_executable(test_packet_vectors
  test_packet_vectors.cpp
  ${ELERO_PACKET_SRC}
)
target_link_libraries(test_packet_vectors GTest::gtest_main)

# Test executable: CommandSender state machine (uses time_provider.cpp)
add_executable(test_command_sender
  test_command_sender.cpp
  ${ELERO_TIME_PROVIDER_SRC}
)
target_compile_definitions(test_command_sender PRIVATE UNIT_TEST)
target_link_libraries(test_command_sender GTest::gtest_main)

# Test executable: Golden vectors from working commit fe33d186f70dd97a3fc8dc8a42bfb94dc3c7a5e5
add_executable(test_golden_vectors
  test_golden_vectors.cpp
  ${ELERO_PACKET_SRC}
)
target_link_libraries(test_golden_vectors GTest::gtest_main)

# Test executable: Known-answer encryption vectors and lookup table integrity
add_executable(test_encryption_vectors
  test_encryption_vectors.cpp
  ${ELERO_PACKET_SRC}
)
target_link_libraries(test_encryption_vectors GTest::gtest_main)

# Test executable: TX->RX roundtrip, valid packet parsing, button packets
add_executable(test_parse_roundtrip
  test_parse_roundtrip.cpp
  ${ELERO_PACKET_SRC}
)
target_link_libraries(test_parse_roundtrip GTest::gtest_main)

# Test executable: String conversion functions (state/command/action)
add_executable(test_string_functions
  test_string_functions.cpp
  ${ELERO_STRINGS_SRC}
)
target_link_libraries(test_string_functions GTest::gtest_main)

# Cover state machine (variant-based)
set(ELERO_COVER_SM_SRC ${COMPONENTS_DIR}/elero/cover_sm.cpp)
add_executable(test_cover_sm
  test_cover_sm.cpp
  ${ELERO_COVER_SM_SRC}
  ${ELERO_PACKET_SRC}
)
target_link_libraries(test_cover_sm GTest::gtest_main)

# Light state machine (variant-based)
set(ELERO_LIGHT_SM_SRC ${COMPONENTS_DIR}/elero/light_sm.cpp)
add_executable(test_light_sm
  test_light_sm.cpp
  ${ELERO_LIGHT_SM_SRC}
  ${ELERO_PACKET_SRC}
)
target_link_libraries(test_light_sm GTest::gtest_main)

# Discover all tests
include(GoogleTest)
gtest_discover_tests(test_protocol)
gtest_discover_tests(test_packet_vectors)
gtest_discover_tests(test_command_sender)
gtest_discover_tests(test_golden_vectors)
gtest_discover_tests(test_encryption_vectors)
gtest_discover_tests(test_parse_roundtrip)
gtest_discover_tests(test_string_functions)
gtest_discover_tests(test_cover_sm)
gtest_discover_tests(test_light_sm)

# All test targets
set(ALL_TEST_TARGETS
  test_protocol test_packet_vectors test_command_sender test_golden_vectors
  test_encryption_vectors test_parse_roundtrip test_string_functions
  test_cover_sm test_light_sm
)

# Combined target for running all tests
add_custom_target(run_all_tests
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  DEPENDS ${ALL_TEST_TARGETS}
)

# JUnit XML output for CI integration
add_custom_target(test_xml
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --output-junit ${CMAKE_BINARY_DIR}/test-results.xml
  DEPENDS ${ALL_TEST_TARGETS}
)
