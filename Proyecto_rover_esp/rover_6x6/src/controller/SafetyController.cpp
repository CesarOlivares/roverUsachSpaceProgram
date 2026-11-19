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

void SafetyController::attempt(uint32_t n)
{
    Log::warn("SAFETY", "Enlace perdido: intento de reconexion %u de %u.",
              (unsigned)n, (unsigned)cfg::kLinkRetryCount);
    if (reconnect_) reconnect_();
}

void SafetyController::loop()
{
    const int64_t timeout_us = (int64_t)cfg::kFailsafeTimeoutMs * 1000;
    const int64_t window_us  = (int64_t)cfg::kLinkRetryWindowMs * 1000;

    uint32_t tries = 0;          // 0 = enlace sano; 1..N = intento en curso

    while (true) {
        if (tries == 0) {
            if (ctrl_.controlLinkLost(timeout_us)) {
                // Lo primero, siempre: las ruedas se paran ya.
                ctrl_.linkLostHold();
                Log::warn("SAFETY", "Sin trafico del operador en %u ms. Traccion cortada; armado conservado.",
                          (unsigned)cfg::kFailsafeTimeoutMs);
                tries = 1;
                attempt(tries);
            }
        } else if (!ctrl_.controlLinkLost(timeout_us)) {
            // Volvio el operador (o nadie tiene ya el mando que proteger).
            Log::info("SAFETY", "Enlace recuperado en el intento %u. El rover sigue armado.",
                      (unsigned)tries);
            tries = 0;
        } else if (ctrl_.controlLinkLost(timeout_us + (int64_t)tries * window_us)) {
            if (tries < cfg::kLinkRetryCount) {
                tries++;
                attempt(tries);
            } else {
                Log::warn("SAFETY", "FAILSAFE: %u intentos sin respuesta. Rover desarmado.",
                          (unsigned)cfg::kLinkRetryCount);
                ctrl_.failsafeStop();
                tries = 0;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(cfg::kFailsafePollMs));
    }
}
