#include "SafetyController.h"

#include "freertos/task.h"
#include "../util/Log.h"

bool SafetyController::begin()
{
    // Sin esto, el primer ciclo ve un silencio de "todo el uptime" y dispara.
    ctrl_.resetLinkTimer();
    return xTaskCreatePinnedToCore(task, "link_failsafe", 3072, this, 7, nullptr, 0) == pdPASS;
}

void SafetyController::task(void *arg)
{
    static_cast<SafetyController *>(arg)->loop();
}

void SafetyController::loop()
{
    const int64_t timeout_us = (int64_t)cfg::kFailsafeTimeoutMs * 1000;

    while (true) {
        if (ctrl_.controlLinkLost(timeout_us)) {
            Log::warn("SAFETY", "FAILSAFE: el operador al mando lleva mas de %u ms sin trafico. Parando.",
                      (unsigned)cfg::kFailsafeTimeoutMs);
            ctrl_.failsafeStop();
        }
        vTaskDelay(pdMS_TO_TICKS(cfg::kFailsafePollMs));
    }
}
