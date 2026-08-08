# nucleo_h7s3l8 Board Definition

# Append to executable
target_sources(${EXECUTABLE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/board_init.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stm32h7rsxx_hal_msp.c
)

target_include_directories(${EXECUTABLE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Apply linker script
if(APP STREQUAL "bootloader")
    set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/STM32H7S3L8HX_FLASH.ld)
    target_compile_definitions(${EXECUTABLE_NAME} PRIVATE BOOTLOADER)
else()
    set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/STM32H7S3L8HX_EXTFLASH.ld)
endif()

target_link_options(${EXECUTABLE_NAME} PRIVATE
    -T${LINKER_SCRIPT}
)
