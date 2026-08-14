#include "FreeRtosThread.h"

namespace stm32 {

FreeRtosThread::FreeRtosThread(osal::Runnable& runnable, const char* name, uint16_t stackSize, UBaseType_t priority)
    : m_runnable(runnable), m_handle(nullptr), m_name(name), m_stackSize(stackSize), m_priority(priority) {
}

FreeRtosThread::~FreeRtosThread() {
    if (m_handle != nullptr) {
        vTaskDelete(m_handle);
    }
}

bool FreeRtosThread::start() {
    if (m_handle != nullptr) return false;
    
    BaseType_t result = xTaskCreate(
        taskRunner,
        m_name,
        m_stackSize,
        this, // Pass 'this' as the parameter
        m_priority,
        &m_handle
    );
    
    return (result == pdPASS);
}

void FreeRtosThread::suspend() {
    if (m_handle != nullptr) {
        vTaskSuspend(m_handle);
    }
}

void FreeRtosThread::resume() {
    if (m_handle != nullptr) {
        vTaskResume(m_handle);
    }
}

void FreeRtosThread::taskRunner(void* parameters) {
    auto* self = static_cast<FreeRtosThread*>(parameters);
    
    // Call the application logic
    self->m_runnable.run();
    
    // If the runnable returns, delete the task
    vTaskDelete(NULL);
}

} // namespace stm32

// OSAL System-wide delay implementation using FreeRTOS
namespace osal {
void Thread::delay(uint32_t milliseconds) {
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}
}
