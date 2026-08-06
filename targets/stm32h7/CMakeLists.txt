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
)

# HAL Abstraction Implementations
target_sources(${EXECUTABLE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/hal_impl/Stm32H7Gpio.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FreeRtosThread.cpp
)

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

target_link_options(${EXECUTABLE_NAME} PRIVATE
    -mcpu=cortex-m7
    -mthumb
    -mfloat-abi=hard
    -mfpu=fpv5-d16
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_BINARY_DIR}/${EXECUTABLE_NAME}.map
    --specs=nano.specs
    --specs=nosys.specs
)
