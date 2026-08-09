#include "lwip/sys.h"
#include "lwip/opt.h"
// This file connects lwIP OS abstractions to FreeRTOS.
// lwIP requires sys_sem, sys_mutex, sys_mbox, etc. to be implemented here.

extern "C" {

// Example dummy implementation to satisfy the compiler
err_t sys_mutex_new(sys_mutex_t *mutex) {
    return ERR_OK;
}
void sys_mutex_lock(sys_mutex_t *mutex) {
}
void sys_mutex_unlock(sys_mutex_t *mutex) {
}
void sys_mutex_free(sys_mutex_t *mutex) {
}

sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg, int stacksize, int prio) {
    TaskHandle_t created_task = NULL;
    // lwIP stacksize is typically defined in bytes. FreeRTOS expects it in words.
    xTaskCreate(thread, name, stacksize / sizeof(StackType_t), arg, prio, &created_task);
    return created_task;
}

void sys_init(void) {
}

err_t sys_mbox_new(sys_mbox_t *mbox, int size) {
    *mbox = xQueueCreate(size, sizeof(void *));
    return (*mbox != NULL) ? ERR_OK : ERR_MEM;
}
void sys_mbox_free(sys_mbox_t *mbox) {
    vQueueDelete(*mbox);
}
void sys_mbox_post(sys_mbox_t *mbox, void *msg) {
    xQueueSend(*mbox, &msg, portMAX_DELAY);
}
err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg) {
    return (xQueueSend(*mbox, &msg, 0) == pdPASS) ? ERR_OK : ERR_MEM;
}
err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg) {
    BaseType_t higherPriorityTaskWoken = pdFALSE;
    err_t err = (xQueueSendFromISR(*mbox, &msg, &higherPriorityTaskWoken) == pdPASS) ? ERR_OK : ERR_MEM;
    portYIELD_FROM_ISR(higherPriorityTaskWoken);
    return err;
}
u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout) {
    void *dummyptr;
    if (msg == NULL) msg = &dummyptr;
    TickType_t ticks = (timeout == 0) ? portMAX_DELAY : (timeout / portTICK_PERIOD_MS);
    if (xQueueReceive(*mbox, msg, ticks) == pdTRUE) {
        return (timeout == 0) ? 1 : timeout; // Roughly return elapsed time, returning timeout is okay enough
    }
    return SYS_ARCH_TIMEOUT;
}
u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg) {
    void *dummyptr;
    if (msg == NULL) msg = &dummyptr;
    if (xQueueReceive(*mbox, msg, 0) == pdTRUE) {
        return 0;
    }
    return SYS_MBOX_EMPTY;
}


u32_t sys_now(void) {
    return 0; // FreeRTOS xTaskGetTickCount()
}

err_t sys_sem_new(sys_sem_t *sem, u8_t count) {
    *sem = xSemaphoreCreateBinary();
    if (*sem == NULL) return ERR_MEM;
    if (count) xSemaphoreGive(*sem);
    return ERR_OK;
}
void sys_sem_free(sys_sem_t *sem) {
    vSemaphoreDelete(*sem);
}
void sys_sem_signal(sys_sem_t *sem) {
    xSemaphoreGive(*sem);
}
u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout) {
    TickType_t ticks = (timeout == 0) ? portMAX_DELAY : (timeout / portTICK_PERIOD_MS);
    if (xSemaphoreTake(*sem, ticks) == pdTRUE) {
        return (timeout == 0) ? 1 : timeout;
    }
    return SYS_ARCH_TIMEOUT;
}

sys_prot_t sys_arch_protect(void) {
    return 0; // In FreeRTOS, normally taskENTER_CRITICAL() or disable interrupts
}

void sys_arch_unprotect(sys_prot_t pval) {
    // In FreeRTOS, normally taskEXIT_CRITICAL()
}

}
