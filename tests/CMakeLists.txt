# EmbeddedPixel Test Suite CMake Configuration
cmake_minimum_required(VERSION 3.20)
project(EmbeddedPixelTests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../components
    ${CMAKE_CURRENT_SOURCE_DIR}/../components/drivers/uim342
    ${CMAKE_CURRENT_SOURCE_DIR}/../core
    ${CMAKE_CURRENT_SOURCE_DIR}/../core/hal
)

# Standalone Unit Test Targets
add_executable(test_proto_structs test_proto_structs.cpp)
add_executable(test_time_source test_time_source.cpp)
add_executable(test_time_manager test_time_manager.cpp)
add_executable(test_e2e_integration test_e2e_integration.cpp)
add_executable(test_uim342_codec 
    test_uim342_codec.cpp
    ../components/drivers/uim342/Uim342Driver.cpp
)

# Enable CTest integration
enable_testing()
add_test(NAME ProtoStructsTest COMMAND test_proto_structs)
add_test(NAME TimeSourceTest COMMAND test_time_source)
add_test(NAME TimeManagerTest COMMAND test_time_manager)
add_test(NAME IntegrationTest COMMAND test_e2e_integration)
add_test(NAME Uim342CodecTest COMMAND test_uim342_codec)
