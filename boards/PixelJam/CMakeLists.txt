# PixelJam Board Definition

# Append board sources to executable
target_sources(${EXECUTABLE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/board_init.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/stm32h7xx_hal_msp.c
)

target_include_directories(${EXECUTABLE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Apply linker script
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/STM32H743xx_FLASH.ld)

target_link_options(${EXECUTABLE_NAME} PRIVATE
    -T${LINKER_SCRIPT}
)
