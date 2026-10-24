#include "MotorDriver.h"

#include <math.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "Pinout.h"
#include "../util/Log.h"

/*
 * Se usa el driver LEDC de ESP-IDF directamente (viene dentro del core de
 * Arduino) en vez de ledcAttach/ledcWrite: asi el canal de cada motor queda
 * fijo y el codigo no depende de la version del core.
 */
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_TIMER      LEDC_TIMER_0
#define LEDC_DUTY_RES   LEDC_TIMER_10_BIT   // 0..1023
#define LEDC_DUTY_MAX   1023
#define LEDC_FREQUENCY  20000               // 20 kHz (fuera del rango audible)

/*
 * Estrategia de accionamiento (sign-magnitude, decaimiento lento):
 *
 *   pin_pwm (IN1) queda ligado PERMANENTEMENTE a un canal LEDC en begin().
 *   pin_dir (IN2) es un GPIO normal que solo indica el sentido.
 *
 *     Adelante : IN2 = 0, duty(IN1) = magnitud
 *     Atras    : IN2 = 1, duty(IN1) = MAX - magnitud
 *     Parado   : IN2 = 0, duty(IN1) = 0   (rueda libre)
 *
 * Esto permite cambiar velocidad y sentido usando solo ledc_set_duty() +
 * ledc_update_duty(), que son escrituras a registro. Nunca se vuelve a llamar
 * a ledc_channel_config() en caliente, asi que la matriz GPIO no se reconfigura
 * y ningun pin queda con una senal PWM huerfana enrutada encima.
 *
 * Tampoco se usa pinMode() sobre estos pines: reinicia el pin y era el origen
 * de las senales parasitas en los motores.
 */

namespace {

struct MotorPins {
    int            pinPwm;   // IN1 - portadora PWM
    int            pinDir;   // IN2 - sentido
    ledc_channel_t channel;
};

const MotorPins kMotors[cfg::kMotorCount] = {
    { pins::kM1In1, pins::kM1In2, LEDC_CHANNEL_0 },  // M1: Frontal Izq
    { pins::kM2In1, pins::kM2In2, LEDC_CHANNEL_1 },  // M2: Frontal Der
    { pins::kM3In1, pins::kM3In2, LEDC_CHANNEL_2 },  // M3: Medio Izq
    { pins::kM4In1, pins::kM4In2, LEDC_CHANNEL_3 },  // M4: Medio Der
    { pins::kM5In1, pins::kM5In2, LEDC_CHANNEL_4 },  // M5: Trasero Izq
    { pins::kM6In1, pins::kM6In2, LEDC_CHANNEL_5 },  // M6: Trasero Der
};

}  // namespace

bool MotorDriver::begin()
{
    if (initialized_) return true;

    Log::info("MOTORS", "Inicializando 6 motores DC (L298N) con LEDC a %d Hz...", LEDC_FREQUENCY);

    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode      = LEDC_MODE;
    timer_cfg.timer_num       = LEDC_TIMER;
    timer_cfg.duty_resolution = LEDC_DUTY_RES;
    timer_cfg.freq_hz         = LEDC_FREQUENCY;
    timer_cfg.clk_cfg         = LEDC_AUTO_CLK;
    if (ledc_timer_config(&timer_cfg) != ESP_OK) {
        Log::warn("MOTORS", "No se pudo configurar el timer LEDC.");
        return false;
    }

    // Pines de sentido: GPIO de salida simple, en bajo.
    uint64_t dir_mask = 0;
    for (const MotorPins &m : kMotors) dir_mask |= (1ULL << m.pinDir);

    gpio_config_t dir_conf = {};
    dir_conf.pin_bit_mask = dir_mask;
    dir_conf.mode         = GPIO_MODE_OUTPUT;
    dir_conf.pull_up_en   = GPIO_PULLUP_DISABLE;
    dir_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    dir_conf.intr_type    = GPIO_INTR_DISABLE;
    if (gpio_config(&dir_conf) != ESP_OK) return false;

    for (const MotorPins &m : kMotors) {
        gpio_set_level((gpio_num_t)m.pinDir, 0);

        // Enlace unico y permanente canal LEDC <-> pin PWM.
        ledc_channel_config_t ch_cfg = {};
        ch_cfg.speed_mode = LEDC_MODE;
        ch_cfg.channel    = m.channel;
        ch_cfg.timer_sel  = LEDC_TIMER;
        ch_cfg.intr_type  = LEDC_INTR_DISABLE;
        ch_cfg.gpio_num   = m.pinPwm;
        ch_cfg.duty       = 0;
        ch_cfg.hpoint     = 0;
        if (ledc_channel_config(&ch_cfg) != ESP_OK) return false;
    }

    initialized_ = true;
    Log::info("MOTORS", "Motores listos (6 canales LEDC enlazados de forma estatica).");
    return true;
}

float MotorDriver::apply(int idx, float speed)
{
    if (!initialized_) return NAN;
    if (idx < 0 || idx >= cfg::kMotorCount) return NAN;
    if (isnan(speed)) return NAN;

    if (speed >  100.0f) speed =  100.0f;
    if (speed < -100.0f) speed = -100.0f;

    const MotorPins &m = kMotors[idx];
    uint32_t magnitude = (uint32_t)((fabsf(speed) / 100.0f) * LEDC_DUTY_MAX + 0.5f);
    if (magnitude > LEDC_DUTY_MAX) magnitude = LEDC_DUTY_MAX;

    uint32_t duty;
    if (speed > 0.0f) {
        gpio_set_level((gpio_num_t)m.pinDir, 0);
        duty = magnitude;
    } else if (speed < 0.0f) {
        gpio_set_level((gpio_num_t)m.pinDir, 1);
        duty = LEDC_DUTY_MAX - magnitude;
    } else {
        gpio_set_level((gpio_num_t)m.pinDir, 0);
        duty = 0;
    }

    ledc_set_duty(LEDC_MODE, m.channel, duty);
    ledc_update_duty(LEDC_MODE, m.channel);
    return speed;
}

void MotorDriver::stopAll()
{
    for (int i = 0; i < cfg::kMotorCount; i++) apply(i, 0.0f);
}
