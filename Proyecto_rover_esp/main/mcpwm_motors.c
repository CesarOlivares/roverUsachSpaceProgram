#include "mcpwm_motors.h"
#include "pinout.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <math.h>

static const char *TAG = "MOTORS";

#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_DUTY_RES           LEDC_TIMER_10_BIT   // 0..1023
#define LEDC_DUTY_MAX           1023
#define LEDC_FREQUENCY          20000               // 20 kHz (fuera del rango audible)

/*
 * Estrategia de accionamiento (sign-magnitude, decaimiento lento):
 *
 *   pin_pwm (IN1) queda ligado PERMANENTEMENTE a un canal LEDC en el init.
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
 * El ESP32-S3 expone 8 canales LEDC en modo low-speed; usamos 6, uno por motor.
 */

typedef struct {
    int           pin_pwm;   // IN1 - portadora PWM
    int           pin_dir;   // IN2 - sentido
    ledc_channel_t channel;
} motor_cfg_t;

static const motor_cfg_t motor_cfg[NUM_MOTORS] = {
    { GPIO_M1_IN1, GPIO_M1_IN2, LEDC_CHANNEL_0 }, // M1: Frontal Izq
    { GPIO_M2_IN1, GPIO_M2_IN2, LEDC_CHANNEL_1 }, // M2: Frontal Der
    { GPIO_M3_IN1, GPIO_M3_IN2, LEDC_CHANNEL_2 }, // M3: Medio Izq
    { GPIO_M4_IN1, GPIO_M4_IN2, LEDC_CHANNEL_3 }, // M4: Medio Der
    { GPIO_M5_IN1, GPIO_M5_IN2, LEDC_CHANNEL_4 }, // M5: Trasero Izq
    { GPIO_M6_IN1, GPIO_M6_IN2, LEDC_CHANNEL_5 }  // M6: Trasero Der
};

static float             g_speeds[NUM_MOTORS] = {0};
static SemaphoreHandle_t g_motor_mutex        = NULL;
static bool              g_initialized        = false;

// Aplica la velocidad al hardware. El llamador debe tener g_motor_mutex tomado.
static void apply_speed_locked(int idx, float speed)
{
    const motor_cfg_t *cfg = &motor_cfg[idx];
    uint32_t magnitude = (uint32_t)((fabsf(speed) / 100.0f) * LEDC_DUTY_MAX + 0.5f);
    if (magnitude > LEDC_DUTY_MAX) magnitude = LEDC_DUTY_MAX;

    uint32_t duty;
    if (speed > 0.0f) {
        gpio_set_level(cfg->pin_dir, 0);
        duty = magnitude;
    } else if (speed < 0.0f) {
        gpio_set_level(cfg->pin_dir, 1);
        duty = LEDC_DUTY_MAX - magnitude;
    } else {
        gpio_set_level(cfg->pin_dir, 0);
        duty = 0;
    }

    ledc_set_duty(LEDC_MODE, cfg->channel, duty);
    ledc_update_duty(LEDC_MODE, cfg->channel);

    g_speeds[idx] = speed;
}

esp_err_t mcpwm_motors_init(void)
{
    if (g_initialized) return ESP_OK;

    ESP_LOGI(TAG, "Inicializando 6 motores DC (L298N) con LEDC a %d Hz...", LEDC_FREQUENCY);

    g_motor_mutex = xSemaphoreCreateMutex();
    if (!g_motor_mutex) return ESP_ERR_NO_MEM;

    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz         = LEDC_FREQUENCY,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    // Pines de sentido: GPIO de salida simple, en bajo.
    uint64_t dir_mask = 0;
    for (int i = 0; i < NUM_MOTORS; i++) {
        dir_mask |= (1ULL << motor_cfg[i].pin_dir);
    }
    gpio_config_t dir_conf = {
        .pin_bit_mask = dir_mask,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&dir_conf));

    for (int i = 0; i < NUM_MOTORS; i++) {
        gpio_set_level(motor_cfg[i].pin_dir, 0);

        // Enlace unico y permanente canal LEDC <-> pin PWM.
        ledc_channel_config_t ch_cfg = {
            .speed_mode = LEDC_MODE,
            .channel    = motor_cfg[i].channel,
            .timer_sel  = LEDC_TIMER,
            .intr_type  = LEDC_INTR_DISABLE,
            .gpio_num   = motor_cfg[i].pin_pwm,
            .duty       = 0,
            .hpoint     = 0,
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

        g_speeds[i] = 0.0f;
    }

    g_initialized = true;
    ESP_LOGI(TAG, "Motores listos (6 canales LEDC enlazados de forma estatica).");
    return ESP_OK;
}

esp_err_t mcpwm_motor_set_speed(int motor_index, float speed)
{
    if (!g_initialized) return ESP_ERR_INVALID_STATE;
    if (motor_index < 0 || motor_index >= NUM_MOTORS) return ESP_ERR_INVALID_ARG;
    if (isnan(speed)) return ESP_ERR_INVALID_ARG;

    if (speed >  100.0f) speed =  100.0f;
    if (speed < -100.0f) speed = -100.0f;

    xSemaphoreTake(g_motor_mutex, portMAX_DELAY);
    apply_speed_locked(motor_index, speed);
    xSemaphoreGive(g_motor_mutex);

    return ESP_OK;
}

void mcpwm_motors_drive_differential(float linear, float angular)
{
    if (!g_initialized) return;

    float left  = linear + angular;
    float right = linear - angular;

    // Escalado proporcional: preserva el radio de giro al saturar.
    float peak = fabsf(left) > fabsf(right) ? fabsf(left) : fabsf(right);
    if (peak > 100.0f) {
        left  = (left  / peak) * 100.0f;
        right = (right / peak) * 100.0f;
    }

    // Un solo bloqueo para los 6 motores: el eje entero cambia de forma atomica.
    xSemaphoreTake(g_motor_mutex, portMAX_DELAY);
    apply_speed_locked(0, left);   // M1 Frontal Izq
    apply_speed_locked(2, left);   // M3 Medio Izq
    apply_speed_locked(4, left);   // M5 Trasero Izq
    apply_speed_locked(1, right);  // M2 Frontal Der
    apply_speed_locked(3, right);  // M4 Medio Der
    apply_speed_locked(5, right);  // M6 Trasero Der
    xSemaphoreGive(g_motor_mutex);
}

void mcpwm_motors_stop_all(void)
{
    if (!g_initialized) return;

    xSemaphoreTake(g_motor_mutex, portMAX_DELAY);
    for (int i = 0; i < NUM_MOTORS; i++) {
        apply_speed_locked(i, 0.0f);
    }
    xSemaphoreGive(g_motor_mutex);
}

bool mcpwm_motors_any_active(void)
{
    if (!g_initialized) return false;

    bool active = false;
    xSemaphoreTake(g_motor_mutex, portMAX_DELAY);
    for (int i = 0; i < NUM_MOTORS; i++) {
        if (g_speeds[i] != 0.0f) { active = true; break; }
    }
    xSemaphoreGive(g_motor_mutex);
    return active;
}

void mcpwm_motors_get_speeds(float speeds[NUM_MOTORS])
{
    if (!g_initialized) {
        for (int i = 0; i < NUM_MOTORS; i++) speeds[i] = 0.0f;
        return;
    }

    xSemaphoreTake(g_motor_mutex, portMAX_DELAY);
    for (int i = 0; i < NUM_MOTORS; i++) {
        speeds[i] = g_speeds[i];
    }
    xSemaphoreGive(g_motor_mutex);
}
