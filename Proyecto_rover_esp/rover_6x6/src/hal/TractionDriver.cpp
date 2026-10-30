#include "TractionDriver.h"

#include <math.h>
#include "driver/ledc.h"
#include "Pinout.h"
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

/*
 * Accionamiento del BTS7960:
 *
 *     Adelante : RPWM = magnitud, LPWM = 0
 *     Atras    : RPWM = 0,        LPWM = magnitud
 *     Parado   : RPWM = 0,        LPWM = 0      (los dos lados bajos: freno)
 *
 * Las dos entradas nunca llevan PWM a la vez: al cambiar de sentido primero se
 * apaga la que estaba activa y despues se enciende la otra.
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
    if (initialized_) return true;

    Log::info("TRACTION", "Inicializando BTS7960 (RPWM %d, LPWM %d) a %d Hz...",
              pins::kTractionRpwm, pins::kTractionLpwm, LEDC_FREQUENCY);

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

    initialized_ = true;
    Log::info("TRACTION", "Traccion lista (parada).");
    return true;
}

float TractionDriver::apply(float speed)
{
    if (!initialized_) return NAN;
    if (isnan(speed)) return NAN;

    if (speed >  100.0f) speed =  100.0f;
    if (speed < -100.0f) speed = -100.0f;

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
    return speed;
}

void TractionDriver::stop()
{
    apply(0.0f);
}
