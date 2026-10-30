#include "TractionDriver.h"

#include <math.h>
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "Pinout.h"
#include "../config/RoverConfig.h"
#include "../util/Log.h"

/*
 * Se usa el driver LEDC de ESP-IDF directamente (viene dentro del core de
 * Arduino) en vez de ledcAttach/ledcWrite: asi cada canal queda fijo a su pin
 * y el codigo no depende de la version del core.
 */
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_DUTY_RES   LEDC_TIMER_10_BIT   // 0..1023
#define LEDC_DUTY_MAX   1023
#define LEDC_FREQUENCY  20000               // 20 kHz: inaudible y dentro de los 25 kHz del BTS7960

#define CH_RPWM         LEDC_CHANNEL_0      // adelante
#define CH_LPWM         LEDC_CHANNEL_1      // atras

#define RAMP_PERIOD_MS  10

/*
 * Accionamiento del BTS7960:
 *
 *     Adelante : RPWM = magnitud, LPWM = 0
 *     Atras    : RPWM = 0,        LPWM = magnitud
 *     Parado   : RPWM = 0,        LPWM = 0      (los dos lados bajos: freno)
 *
 * Las dos entradas nunca llevan PWM a la vez: al cambiar de sentido primero se
 * apaga la que estaba activa y despues se enciende la otra. Ademas la rampa
 * pasa siempre por 0 al invertir el sentido.
 *
 * Los canales se enlazan a sus pines una sola vez en begin(); en caliente solo
 * se escribe duty, asi la matriz GPIO no se reconfigura y no quedan senales
 * huerfanas enrutadas sobre un pin. Tampoco se usa pinMode() sobre estos pines.
 */

namespace {

void setDuty(ledc_channel_t ch, uint32_t duty)
{
    ledc_set_duty(LEDC_MODE, ch, duty);
    ledc_update_duty(LEDC_MODE, ch);
}

bool attach(ledc_channel_t ch, int pin)
{
    ledc_channel_config_t cfg = {};
    cfg.speed_mode = LEDC_MODE;
    cfg.channel    = ch;
    cfg.timer_sel  = LEDC_TIMER;
    cfg.intr_type  = LEDC_INTR_DISABLE;
    cfg.gpio_num   = pin;
    cfg.duty       = 0;
    cfg.hpoint     = 0;
    return ledc_channel_config(&cfg) == ESP_OK;
}

}  // namespace

bool TractionDriver::begin()
{
    if (mtx_) return true;

    Log::info("TRACTION", "Inicializando BTS7960 (RPWM %d, LPWM %d, EN %d) a %d Hz...",
              pins::kTractionRpwm, pins::kTractionLpwm, pins::kTractionEn, LEDC_FREQUENCY);

    mtx_ = xSemaphoreCreateMutex();
    if (!mtx_) return false;

    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode      = LEDC_MODE;
    timer_cfg.timer_num       = LEDC_TIMER;
    timer_cfg.duty_resolution = LEDC_DUTY_RES;
    timer_cfg.freq_hz         = LEDC_FREQUENCY;
    timer_cfg.clk_cfg         = LEDC_AUTO_CLK;
    if (ledc_timer_config(&timer_cfg) != ESP_OK) {
        Log::warn("TRACTION", "No se pudo configurar el timer LEDC.");
        return false;
    }

    if (!attach(CH_RPWM, pins::kTractionRpwm)) return false;
    if (!attach(CH_LPWM, pins::kTractionLpwm)) return false;

    // Con las dos entradas ya a 0, se habilita la etapa de potencia.
    if (pins::kTractionEn >= 0) {
        gpio_config_t en = {};
        // La guarda evita un desplazamiento negativo cuando kTractionEn = -1.
        en.pin_bit_mask = 1ULL << (pins::kTractionEn >= 0 ? pins::kTractionEn : 0);
        en.mode         = GPIO_MODE_OUTPUT;
        if (gpio_config(&en) != ESP_OK) return false;
        gpio_set_level((gpio_num_t)pins::kTractionEn, 1);
    }

    if (xTaskCreatePinnedToCore(task, "traction_ramp", 2048, this, 5, nullptr, 1) != pdPASS) {
        return false;
    }

    Log::info("TRACTION", "Traccion lista (parada, rampa %.0f %%/s).", cfg::kTractionRampPctPerS);
    return true;
}

void TractionDriver::writeOutput(float speed)
{
    current_ = speed;

    uint32_t magnitude = (uint32_t)((fabsf(speed) / 100.0f) * LEDC_DUTY_MAX + 0.5f);
    if (magnitude > LEDC_DUTY_MAX) magnitude = LEDC_DUTY_MAX;

    if (speed > 0.0f) {
        setDuty(CH_LPWM, 0);                    // primero se apaga el sentido contrario
        setDuty(CH_RPWM, magnitude);
    } else if (speed < 0.0f) {
        setDuty(CH_RPWM, 0);
        setDuty(CH_LPWM, magnitude);
    } else {
        setDuty(CH_RPWM, 0);
        setDuty(CH_LPWM, 0);
    }
}

float TractionDriver::setTarget(float speed)
{
    if (!mtx_ || isnan(speed)) return NAN;
    if (speed >  100.0f) speed =  100.0f;
    if (speed < -100.0f) speed = -100.0f;

    xSemaphoreTake(mtx_, portMAX_DELAY);
    target_ = speed;
    xSemaphoreGive(mtx_);
    return speed;
}

void TractionDriver::stopNow()
{
    if (!mtx_) return;
    // Mismo mutex que la rampa: tras volver de aqui, ninguna escritura vieja
    // de la tarea puede reactivar el puente.
    xSemaphoreTake(mtx_, portMAX_DELAY);
    target_ = 0.0f;
    writeOutput(0.0f);
    xSemaphoreGive(mtx_);
}

void TractionDriver::task(void *arg) { static_cast<TractionDriver *>(arg)->loop(); }

void TractionDriver::loop()
{
    const float step = cfg::kTractionRampPctPerS * (RAMP_PERIOD_MS / 1000.0f);
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        xSemaphoreTake(mtx_, portMAX_DELAY);
        if (current_ != target_) {
            float next = current_;
            if (next < target_) {
                next += step;
                if (next > target_) next = target_;
            } else {
                next -= step;
                if (next < target_) next = target_;
            }
            writeOutput(next);
        }
        xSemaphoreGive(mtx_);

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(RAMP_PERIOD_MS));
    }
}
