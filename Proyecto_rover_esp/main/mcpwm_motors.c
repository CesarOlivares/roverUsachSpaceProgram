#include "mcpwm_motors.h"
#include "pinout.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "MCPWM_MOTORS";

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES           LEDC_TIMER_10_BIT // 0..1023
#define LEDC_FREQUENCY          20000             // 20 kHz PWM

typedef struct {
    int pin_a;
    int pin_b;
    ledc_channel_t ledc_channel;
} motor_pin_cfg_t;

static const motor_pin_cfg_t motor_cfg[NUM_MOTORS] = {
    { GPIO_M1_IN1, GPIO_M1_IN2, LEDC_CHANNEL_0 }, // M1: FL
    { GPIO_M2_IN1, GPIO_M2_IN2, LEDC_CHANNEL_1 }, // M2: FR
    { GPIO_M3_IN1, GPIO_M3_IN2, LEDC_CHANNEL_2 }, // M3: ML
    { GPIO_M4_IN1, GPIO_M4_IN2, LEDC_CHANNEL_3 }, // M4: MR
    { GPIO_M5_IN1, GPIO_M5_IN2, LEDC_CHANNEL_4 }, // M5: RL
    { GPIO_M6_IN1, GPIO_M6_IN2, LEDC_CHANNEL_5 }  // M6: RR
};

static float g_current_speeds[NUM_MOTORS] = {0.0f};

esp_err_t mcpwm_motors_init(void) {
    ESP_LOGI(TAG, "Initializing 6x6 DC Motors with 20kHz LEDC PWM (L298N)...");

    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Reset and initialize all motor pins as GPIO outputs driven LOW, pull-ups disabled
    for (int i = 0; i < NUM_MOTORS; i++) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << motor_cfg[i].pin_a) | (1ULL << motor_cfg[i].pin_b),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
        gpio_set_level(motor_cfg[i].pin_a, 0);
        gpio_set_level(motor_cfg[i].pin_b, 0);
    }

    mcpwm_motors_stop_all();
    ESP_LOGI(TAG, "Motors initialized successfully.");
    return ESP_OK;
}

esp_err_t mcpwm_motor_set_speed(int motor_index, float speed) {
    if (motor_index < 0 || motor_index >= NUM_MOTORS) return ESP_ERR_INVALID_ARG;

    if (speed > 100.0f) speed = 100.0f;
    if (speed < -100.0f) speed = -100.0f;

    g_current_speeds[motor_index] = speed;

    const motor_pin_cfg_t *cfg = &motor_cfg[motor_index];
    uint32_t duty = (uint32_t)((fabsf(speed) / 100.0f) * 1023.0f);

    if (speed > 0.0f) {
        // Set pin_b to constant LOW
        gpio_set_direction(cfg->pin_b, GPIO_MODE_OUTPUT);
        gpio_set_level(cfg->pin_b, 0);

        // Bind and output PWM on pin_a
        ledc_channel_config_t ledc_ch = {
            .speed_mode     = LEDC_MODE,
            .channel        = cfg->ledc_channel,
            .timer_sel      = LEDC_TIMER,
            .intr_type      = LEDC_INTR_DISABLE,
            .gpio_num       = cfg->pin_a,
            .duty           = duty,
            .hpoint         = 0
        };
        ledc_channel_config(&ledc_ch);
    } else if (speed < 0.0f) {
        // Set pin_a to constant LOW
        gpio_set_direction(cfg->pin_a, GPIO_MODE_OUTPUT);
        gpio_set_level(cfg->pin_a, 0);

        // Bind and output PWM on pin_b
        ledc_channel_config_t ledc_ch = {
            .speed_mode     = LEDC_MODE,
            .channel        = cfg->ledc_channel,
            .timer_sel      = LEDC_TIMER,
            .intr_type      = LEDC_INTR_DISABLE,
            .gpio_num       = cfg->pin_b,
            .duty           = duty,
            .hpoint         = 0
        };
        ledc_channel_config(&ledc_ch);
    } else {
        // Stop: Set both to constant LOW
        gpio_set_direction(cfg->pin_a, GPIO_MODE_OUTPUT);
        gpio_set_level(cfg->pin_a, 0);

        gpio_set_direction(cfg->pin_b, GPIO_MODE_OUTPUT);
        gpio_set_level(cfg->pin_b, 0);

        ledc_set_duty(LEDC_MODE, cfg->ledc_channel, 0);
        ledc_update_duty(LEDC_MODE, cfg->ledc_channel);
    }

    return ESP_OK;
}

void mcpwm_motors_drive_differential(float linear, float angular) {
    float left_speed  = linear + angular;
    float right_speed = linear - angular;

    float max_val = fabsf(left_speed);
    if (fabsf(right_speed) > max_val) max_val = fabsf(right_speed);
    if (max_val > 100.0f) {
        left_speed = (left_speed / max_val) * 100.0f;
        right_speed = (right_speed / max_val) * 100.0f;
    }

    // Left wheels: M1 (0), M3 (2), M5 (4)
    mcpwm_motor_set_speed(0, left_speed);
    mcpwm_motor_set_speed(2, left_speed);
    mcpwm_motor_set_speed(4, left_speed);

    // Right wheels: M2 (1), M4 (3), M6 (5)
    mcpwm_motor_set_speed(1, right_speed);
    mcpwm_motor_set_speed(3, right_speed);
    mcpwm_motor_set_speed(5, right_speed);
}

void mcpwm_motors_stop_all(void) {
    for (int i = 0; i < NUM_MOTORS; i++) {
        mcpwm_motor_set_speed(i, 0.0f);
    }
}

void mcpwm_motors_get_speeds(float speeds[NUM_MOTORS]) {
    for (int i = 0; i < NUM_MOTORS; i++) {
        speeds[i] = g_current_speeds[i];
    }
}
