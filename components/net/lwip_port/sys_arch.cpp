#include "lwip/sys.h"
#include "lwip/opt.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

// This file connects lwIP OS abstractions to FreeRTOS.

extern "C" {

err_t sys_mutex_new(sys_mutex_t *mutex) {
    *mutex = xSemaphoreCreateMutex();
    return (*mutex != NULL) ? ERR_OK : ERR_MEM;
}

void sys_mutex_lock(sys_mutex_t *mutex) {
    xSemaphoreTake(*mutex, portMAX_DELAY);
}

void sys_mutex_unlock(sys_mutex_t *mutex) {
    xSemaphoreGive(*mutex);
}

void sys_mutex_free(sys_mutex_t *mutex) {
    vSemaphoreDelete(*mutex);
}

sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg, int stacksize, int prio) {
    TaskHandle_t created_task = NULL;
    // lwIP stacksize is defined in bytes. FreeRTOS expects stack size in words.
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

u32_t sys_now(void) {
    return (u32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout) {
    void *dummyptr;
    if (msg == NULL) msg = &dummyptr;

    u32_t start_time = sys_now();
    TickType_t ticks = (timeout == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout);

    if (xQueueReceive(*mbox, msg, ticks) == pdTRUE) {
        u32_t elapsed = sys_now() - start_time;
        return (elapsed == 0) ? 1 : elapsed;
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

err_t sys_sem_new(sys_sem_t *sem, u8_t count) {
    *sem = xSemaphoreCreateCounting(255, count);
    return (*sem != NULL) ? ERR_OK : ERR_MEM;
}

void sys_sem_free(sys_sem_t *sem) {
    vSemaphoreDelete(*sem);
}

void sys_sem_signal(sys_sem_t *sem) {
    xSemaphoreGive(*sem);
}

u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout) {
    u32_t start_time = sys_now();
    TickType_t ticks = (timeout == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout);

    if (xSemaphoreTake(*sem, ticks) == pdTRUE) {
        u32_t elapsed = sys_now() - start_time;
        return (elapsed == 0) ? 1 : elapsed;
    }
    return SYS_ARCH_TIMEOUT;
}

sys_prot_t sys_arch_protect(void) {
    taskENTER_CRITICAL();
    return 1;
}

void sys_arch_unprotect(sys_prot_t pval) {
    (void)pval;
    taskEXIT_CRITICAL();
}

} // extern "C"
