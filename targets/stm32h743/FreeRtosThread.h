#pragma once
#include "Thread.h"
#include "FreeRTOS.h"
#include "task.h"

namespace stm32 {

class FreeRtosThread : public osal::Thread {
public:
    FreeRtosThread(osal::Runnable& runnable, const char* name, uint16_t stackSize, UBaseType_t priority);
    ~FreeRtosThread() override;

    bool start() override;
    void suspend() override;
    void resume() override;

private:
    static void taskRunner(void* parameters);

    osal::Runnable& m_runnable;
    TaskHandle_t m_handle;
    const char* m_name;
    uint16_t m_stackSize;
    UBaseType_t m_priority;
};

} // namespace stm32
