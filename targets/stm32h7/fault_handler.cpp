#include <stdio.h>
#include "stm32h7rsxx_hal.h"

extern "C" {

void hard_fault_handler_c(uint32_t *args) {
    uint32_t r0  = args[0];
    uint32_t r1  = args[1];
    uint32_t r2  = args[2];
    uint32_t r3  = args[3];
    uint32_t r12 = args[4];
    uint32_t lr  = args[5];
    uint32_t pc  = args[6];
    uint32_t psr = args[7];

    printf("\r\n========================================\r\n");
    printf("!!! HARDFAULT DETECTED !!!\r\n");
    printf("PC  = 0x%08lX\r\n", pc);
    printf("LR  = 0x%08lX\r\n", lr);
    printf("R0  = 0x%08lX  R1  = 0x%08lX\r\n", r0, r1);
    printf("R2  = 0x%08lX  R3  = 0x%08lX\r\n", r2, r3);
    printf("R12 = 0x%08lX  PSR = 0x%08lX\r\n", r12, psr);
    printf("SCB->CFSR = 0x%08lX\r\n", (unsigned long)SCB->CFSR);
    printf("SCB->HFSR = 0x%08lX\r\n", (unsigned long)SCB->HFSR);
    printf("SCB->MMFAR= 0x%08lX\r\n", (unsigned long)SCB->MMFAR);
    printf("SCB->BFAR = 0x%08lX\r\n", (unsigned long)SCB->BFAR);
    printf("========================================\r\n");

    while (1) {}
}

__attribute__((naked)) void HardFault_Handler(void) {
    __asm volatile (
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "b hard_fault_handler_c\n"
    );
}

__attribute__((naked)) void MemManage_Handler(void) { HardFault_Handler(); }
__attribute__((naked)) void BusFault_Handler(void) { HardFault_Handler(); }
__attribute__((naked)) void UsageFault_Handler(void) { HardFault_Handler(); }

void vApplicationStackOverflowHook(void* xTask, char* pcTaskName) {
    (void)xTask;
    printf("\r\n!!! STACK OVERFLOW DETECTED in task: %s !!!\r\n", pcTaskName);
    while (1) {}
}

} // extern "C"
