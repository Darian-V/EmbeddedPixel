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
set(core_includes
    ${CMAKE_SOURCE_DIR}/core/hal
    ${CMAKE_SOURCE_DIR}/core/osal
    ${CMAKE_SOURCE_DIR}/components/tasks
    ${CMAKE_SOURCE_DIR}/components/sys
)

target_include_directories(${EXECUTABLE_NAME} PRIVATE
    ${core_includes}
)

set(core_sources
    ${CMAKE_SOURCE_DIR}/components/sys/console.cpp
)

target_sources(${EXECUTABLE_NAME} PRIVATE
    ${core_sources}
)

# Application Sources — apps live under boards/${BOARD}/apps/${APP}/
if(APP STREQUAL "bootloader")
    target_include_directories(${EXECUTABLE_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/boards/${BOARD}/apps/${APP}
        ${CMAKE_SOURCE_DIR}/vendor/stm32h7rs/Middlewares/ST/STM32_ExtMem_Manager
        ${CMAKE_SOURCE_DIR}/vendor/stm32h7rs/Middlewares/ST/STM32_ExtMem_Manager/nor_sfdp
        ${CMAKE_SOURCE_DIR}/vendor/stm32h7rs/Middlewares/ST/STM32_ExtMem_Manager/sal
    )
    target_sources(${EXECUTABLE_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/boards/${BOARD}/apps/${APP}/main.cpp
        ${CMAKE_SOURCE_DIR}/boards/${BOARD}/apps/${APP}/extmem_manager.c
        ${CMAKE_SOURCE_DIR}/vendor/stm32h7rs/Middlewares/ST/STM32_ExtMem_Manager/stm32_extmem.c
        ${CMAKE_SOURCE_DIR}/vendor/stm32h7rs/Middlewares/ST/STM32_ExtMem_Manager/nor_sfdp/stm32_sfdp_data.c
        ${CMAKE_SOURCE_DIR}/vendor/stm32h7rs/Middlewares/ST/STM32_ExtMem_Manager/nor_sfdp/stm32_sfdp_driver.c
        ${CMAKE_SOURCE_DIR}/vendor/stm32h7rs/Middlewares/ST/STM32_ExtMem_Manager/sal/stm32_sal_xspi.c
    )
else()
    target_sources(${EXECUTABLE_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/boards/${BOARD}/apps/${APP}/main.cpp
    )
    if(APP STREQUAL "ethernetdev")
        target_include_directories(${EXECUTABLE_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/components/net
            ${CMAKE_SOURCE_DIR}/components/net/lwip_port
        )
        target_sources(${EXECUTABLE_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/components/net/NetManager.cpp
        )
    endif()
    if(APP STREQUAL "blinky" OR APP STREQUAL "ethernetdev")
        target_sources(${EXECUTABLE_NAME} PRIVATE
            ${CMAKE_SOURCE_DIR}/components/tasks/BlinkTask.cpp
        )
    endif()
endif()

# Load Board Configuration
add_subdirectory(boards/${BOARD})

# Load Target Configuration
add_subdirectory(targets/${TARGET})

# Load Vendor Libraries (FreeRTOS, lwIP)
if(NOT APP STREQUAL "bootloader")
    add_subdirectory(vendor)
    
    # Network Configuration
    set(NET_LOG_LEVEL "3" CACHE STRING "Net log verbosity: 0=off 1=err 2=info 3=debug" FORCE)
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE NET_LOG_LEVEL=3)


    configure_file(
        ${CMAKE_SOURCE_DIR}/cmake/net_config.h.in
        ${CMAKE_BINARY_DIR}/net_config.h
    )
    target_include_directories(${EXECUTABLE_NAME} PRIVATE ${CMAKE_BINARY_DIR})
endif()


# Print configuration
message(STATUS "----------------------------------------")
message(STATUS "Building Application : ${APP}")
message(STATUS "Target Board         : ${BOARD}")
message(STATUS "Target MCU           : ${TARGET}")
message(STATUS "----------------------------------------")

# Generate .bin and .hex, then stage artifacts into boards/${BOARD}/apps/${APP}/programming_files/
add_custom_command(TARGET ${EXECUTABLE_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary ${EXECUTABLE_NAME} ${EXECUTABLE_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex   ${EXECUTABLE_NAME} ${EXECUTABLE_NAME}.hex
    COMMAND ${CMAKE_COMMAND} -E make_directory
        ${CMAKE_SOURCE_DIR}/boards/${BOARD}/apps/${APP}/programming_files
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/${EXECUTABLE_NAME}.bin
        ${CMAKE_SOURCE_DIR}/boards/${BOARD}/apps/${APP}/programming_files/${EXECUTABLE_NAME}.bin
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/${EXECUTABLE_NAME}.hex
        ${CMAKE_SOURCE_DIR}/boards/${BOARD}/apps/${APP}/programming_files/${EXECUTABLE_NAME}.hex
    COMMENT "Generating .bin/.hex and copying to boards/${BOARD}/apps/${APP}/programming_files/"
)
