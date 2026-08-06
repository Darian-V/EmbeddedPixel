cmake_minimum_required(VERSION 3.20)

# Build parameters (passed via CLI or set to defaults)
set(APP "blinky" CACHE STRING "The application to build")
set(BOARD "nucleo_h7s3l8" CACHE STRING "The target board")
set(TARGET "stm32h7" CACHE STRING "The target microcontroller family")

# Toolchain configuration (must happen before project or enable_language if setting compilers in toolchain)
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/arm-none-eabi-gcc.cmake)

project(${APP}_${BOARD} C CXX ASM)

# Define executable
set(EXECUTABLE_NAME ${PROJECT_NAME})
add_executable(${EXECUTABLE_NAME})

# Common Include Directories
target_include_directories(${EXECUTABLE_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/core/osal
    ${CMAKE_SOURCE_DIR}/core/hal
    ${CMAKE_SOURCE_DIR}/components/tasks
)

# Application Sources
target_sources(${EXECUTABLE_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/apps/${APP}/main.cpp
    ${CMAKE_SOURCE_DIR}/components/tasks/BlinkTask.cpp
)

# Load Board Configuration
add_subdirectory(boards/${BOARD})

# Load Target Configuration
add_subdirectory(targets/${TARGET})

# Load Vendor Libraries (FreeRTOS)
add_subdirectory(vendor)

# Print configuration
message(STATUS "----------------------------------------")
message(STATUS "Building Application : ${APP}")
message(STATUS "Target Board         : ${BOARD}")
message(STATUS "Target MCU           : ${TARGET}")
message(STATUS "----------------------------------------")

# Generate .bin and .hex
add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary ${EXECUTABLE_NAME} ${EXECUTABLE_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${EXECUTABLE_NAME} ${EXECUTABLE_NAME}.hex
    COMMENT "Generating .bin and .hex files"
)
