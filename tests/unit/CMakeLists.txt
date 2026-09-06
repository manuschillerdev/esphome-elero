cmake_minimum_required(VERSION 3.14)
project(elero_unit_tests)

set(CMAKE_CXX_STANDARD 20)
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
set(ELERO_COVER_SM_SRC ${COMPONENTS_DIR}/elero/cover_sm.cpp)
set(ELERO_LIGHT_SM_SRC ${COMPONENTS_DIR}/elero/light_sm.cpp)
set(ELERO_STATE_SNAPSHOT_SRC ${COMPONENTS_DIR}/elero/state_snapshot.cpp)
set(ELERO_REGISTRY_SRC ${COMPONENTS_DIR}/elero/device_registry.cpp)
set(ELERO_LEARN_IN_SRC ${COMPONENTS_DIR}/elero/learn_in_manager.cpp)
set(ELERO_SEMTECH_TX_FSM_SRC ${COMPONENTS_DIR}/elero/semtech_tx_fsm.cpp)

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
add_executable(test_cover_sm
  test_cover_sm.cpp
  ${ELERO_COVER_SM_SRC}
  ${ELERO_PACKET_SRC}
)
target_link_libraries(test_cover_sm GTest::gtest_main)

# Light state machine (variant-based)
add_executable(test_light_sm
  test_light_sm.cpp
  ${ELERO_LIGHT_SM_SRC}
  ${ELERO_PACKET_SRC}
)
target_link_libraries(test_light_sm GTest::gtest_main)

# PollTimer (pure struct, no ESPHome deps)
add_executable(test_poll_timer
  test_poll_timer.cpp
  ${ELERO_PACKET_SRC}
)
target_link_libraries(test_poll_timer GTest::gtest_main)

# State snapshot — excluded from build: state_snapshot.h depends on
# esphome/components/json/json_util.h which requires ArduinoJson stubs.
# The test file exists (test_state_snapshot.cpp) but cannot compile on host yet.

# DeviceRegistry (command dispatch, RF routing, CRUD, loop logic)
# Unity-build: test file #includes production .cpp files directly to control stub ordering
add_executable(test_device_registry
  test_device_registry.cpp
  ${ELERO_PACKET_SRC}
  ${ELERO_STRINGS_SRC}
  ${ELERO_TIME_PROVIDER_SRC}
  ${ELERO_LEARN_IN_SRC}
)
target_compile_definitions(test_device_registry PRIVATE UNIT_TEST)
target_link_libraries(test_device_registry GTest::gtest_main)

# Group button packet building (0x44 multi-dest TX)
add_executable(test_group_packet
  test_group_packet.cpp
  ${ELERO_PACKET_SRC}
)
target_link_libraries(test_group_packet GTest::gtest_main)

# Learn-in manager (provisioning TX flow)
add_executable(test_learn_in_manager
  test_learn_in_manager.cpp
  ${ELERO_LEARN_IN_SRC}
  ${ELERO_TIME_PROVIDER_SRC}
)
target_compile_definitions(test_learn_in_manager PRIVATE UNIT_TEST)
target_link_libraries(test_learn_in_manager GTest::gtest_main)

# CC1101-compatible CRC-16 and PN9 whitening (pure functions, used by SX1262/SX1276 drivers)
add_executable(test_cc1101_compat
  test_cc1101_compat.cpp
)
target_link_libraries(test_cc1101_compat GTest::gtest_main)

# CC1101 TX FSM orchestration (phase ownership, recovery, abort semantics)
add_executable(test_cc1101_tx_fsm
  test_cc1101_tx_fsm.cpp
  ${COMPONENTS_DIR}/elero/cc1101_tx_fsm.cpp
)
target_link_libraries(test_cc1101_tx_fsm GTest::gtest_main)

# CC1101 TX helper semantics (fallback success, strict TXBYTES, RX restore)
add_executable(test_cc1101_driver_tx
  test_cc1101_driver_tx.cpp
)
target_compile_definitions(test_cc1101_driver_tx PRIVATE UNIT_TEST)
target_link_libraries(test_cc1101_driver_tx GTest::gtest_main)

# Frequency register conversion: CC1101 → SX1262/SX1276 (pure math, no hardware deps)
add_executable(test_freq_conversion
  test_freq_conversion.cpp
)
target_link_libraries(test_freq_conversion GTest::gtest_main)

# Shared Semtech sequencing and production driver behavior.
add_executable(test_semtech_tx_fsm test_semtech_tx_fsm.cpp ${ELERO_SEMTECH_TX_FSM_SRC})
target_link_libraries(test_semtech_tx_fsm GTest::gtest_main)
foreach(chip sx1262 sx1276)
  add_executable(test_${chip}_driver test_${chip}_driver.cpp)
  target_compile_definitions(test_${chip}_driver PRIVATE UNIT_TEST)
  target_link_libraries(test_${chip}_driver GTest::gtest_main)
endforeach()

# Discover all tests
include(GoogleTest)
gtest_discover_tests(test_cc1101_compat)
gtest_discover_tests(test_cc1101_tx_fsm)
gtest_discover_tests(test_cc1101_driver_tx)
gtest_discover_tests(test_freq_conversion)
gtest_discover_tests(test_semtech_tx_fsm)
gtest_discover_tests(test_sx1262_driver)
gtest_discover_tests(test_sx1276_driver)
gtest_discover_tests(test_packet_vectors)
gtest_discover_tests(test_command_sender)
gtest_discover_tests(test_golden_vectors)
gtest_discover_tests(test_encryption_vectors)
gtest_discover_tests(test_parse_roundtrip)
gtest_discover_tests(test_string_functions)
gtest_discover_tests(test_cover_sm)
gtest_discover_tests(test_light_sm)
gtest_discover_tests(test_poll_timer)
gtest_discover_tests(test_group_packet)
gtest_discover_tests(test_learn_in_manager)
gtest_discover_tests(test_device_registry)

# All test targets
set(ALL_TEST_TARGETS
  test_cc1101_compat test_cc1101_tx_fsm test_cc1101_driver_tx
  test_freq_conversion test_semtech_tx_fsm test_sx1262_driver test_sx1276_driver
  test_packet_vectors test_command_sender test_golden_vectors
  test_encryption_vectors test_parse_roundtrip test_string_functions
  test_cover_sm test_light_sm test_poll_timer
  test_group_packet test_learn_in_manager test_device_registry
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
