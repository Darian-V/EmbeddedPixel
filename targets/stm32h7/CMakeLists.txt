# STM32H7 MCU Definition

target_compile_definitions(${EXECUTABLE_NAME} PRIVATE
    USE_HAL_DRIVER
    STM32H7S3xx
)

target_include_directories(${EXECUTABLE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/hal_impl
    ${CMAKE_SOURCE_DIR}/vendor/stm32cubeh7rs/Drivers/STM32H7RSxx_HAL_Driver/Inc
    ${CMAKE_SOURCE_DIR}/vendor/stm32cubeh7rs/Drivers/CMSIS/Device/ST/STM32H7RSxx/Include
    ${CMAKE_SOURCE_DIR}/vendor/stm32cubeh7rs/Drivers/CMSIS/Include
)

# MCU System and Startup Sources
target_sources(${EXECUTABLE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/startup_stm32h7s3l8hx.s
    ${CMAKE_CURRENT_SOURCE_DIR}/system_stm32h7rsxx.c
    ${CMAKE_CURRENT_SOURCE_DIR}/stm32h7_it.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fault_handler.cpp
)

# HAL Abstraction Implementations
target_sources(${EXECUTABLE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/hal_impl/Stm32H7Gpio.cpp
)

if(NOT APP STREQUAL "bootloader")
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE USE_FREERTOS)
    target_sources(${EXECUTABLE_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/hal_impl/Stm32H7Eth.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/hal_impl/Lan8742Phy.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/FreeRtosThread.cpp
    )
endif()


# STM32 Vendor HAL Sources
file(GLOB HAL_SRCS 
    "${CMAKE_SOURCE_DIR}/vendor/stm32cubeh7rs/Drivers/STM32H7RSxx_HAL_Driver/Src/*.c"
)
# Exclude templates
list(FILTER HAL_SRCS EXCLUDE REGEX ".*_template\\.c$")

target_sources(${EXECUTABLE_NAME} PRIVATE ${HAL_SRCS})

# Target-specific flags
target_compile_options(${EXECUTABLE_NAME} PRIVATE
    -mcpu=cortex-m7
    -mthumb
    -mfloat-abi=hard
    -mfpu=fpv5-d16
    -Wall
    -Wextra
    -O0
    -g3
    -fno-exceptions
    -fno-rtti
)

# Target-specific link flags
target_link_options(${EXECUTABLE_NAME} PRIVATE
    -mcpu=cortex-m7
    -mthumb
    -mfloat-abi=hard
    -mfpu=fpv5-d16
    -Wl,-Map=${CMAKE_BINARY_DIR}/${EXECUTABLE_NAME}.map,--cref
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    --specs=nano.specs
    --specs=nosys.specs
)
