# nucleo_h7s3l8 Board Definition

# Append to executable
target_sources(${EXECUTABLE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/board_init.cpp
)

target_include_directories(${EXECUTABLE_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Apply linker script
set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/STM32H7S3L8HX_FLASH.ld)
target_link_options(${EXECUTABLE_NAME} PRIVATE
    -T${LINKER_SCRIPT}
)
