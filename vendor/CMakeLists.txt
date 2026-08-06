# FreeRTOS Kernel build configuration

set(FREERTOS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/FreeRTOS-Kernel)

# Exclude FreeRTOS compilation for mock target
if (NOT ${TARGET} STREQUAL "mock")

    # Add FreeRTOS sources
    target_sources(${EXECUTABLE_NAME} PRIVATE
        ${FREERTOS_DIR}/tasks.c
        ${FREERTOS_DIR}/queue.c
        ${FREERTOS_DIR}/list.c
        ${FREERTOS_DIR}/timers.c
        
        # GCC ARM CM7 Port
        ${FREERTOS_DIR}/portable/GCC/ARM_CM7/r0p1/port.c
        
        # Heap implementation (using heap_4 which is robust and supports fragmentation)
        ${FREERTOS_DIR}/portable/MemMang/heap_4.c
    )

    # Add FreeRTOS include directories
    target_include_directories(${EXECUTABLE_NAME} PRIVATE
        ${FREERTOS_DIR}/include
        ${FREERTOS_DIR}/portable/GCC/ARM_CM7/r0p1
    )

endif()
