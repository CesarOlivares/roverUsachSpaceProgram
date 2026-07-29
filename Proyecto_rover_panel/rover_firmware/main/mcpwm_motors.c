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
    int pwm_pin;
    int dir_a_pin;
    int dir_b_pin;
    ledc_channel_t ledc_channel;
} motor_pin_cfg_t;

static const motor_pin_cfg_t motor_cfg[NUM_MOTORS] = {
    { GPIO_M1_PWM, GPIO_M1_DIR_A, GPIO_M1_DIR_B, LEDC_CHANNEL_0 }, // M1: FL
    { GPIO_M2_PWM, GPIO_M2_DIR_A, GPIO_M2_DIR_B, LEDC_CHANNEL_1 }, // M2: FR
    { GPIO_M3_PWM, GPIO_M3_DIR_A, GPIO_M3_DIR_B, LEDC_CHANNEL_2 }, // M3: ML
    { GPIO_M4_PWM, GPIO_M4_DIR_A, GPIO_M4_DIR_B, LEDC_CHANNEL_3 }, // M4: MR
    { GPIO_M5_PWM, GPIO_M5_DIR_A, GPIO_M5_DIR_B, LEDC_CHANNEL_4 }, // M5: RL
    { GPIO_M6_PWM, GPIO_M6_DIR_A, GPIO_M6_DIR_B, LEDC_CHANNEL_5 }  // M6: RR
};

static float g_current_speeds[NUM_MOTORS] = {0.0f};

esp_err_t mcpwm_motors_init(void) {
    ESP_LOGI(TAG, "Initializing 6x6 DC Motors with 20kHz LEDC PWM...");

    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 0,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    for (int i = 0; i < NUM_MOTORS; i++) {
        io_conf.pin_bit_mask |= (1ULL << motor_cfg[i].dir_a_pin);
        io_conf.pin_bit_mask |= (1ULL << motor_cfg[i].dir_b_pin);

        ledc_channel_config_t ledc_ch = {
            .speed_mode     = LEDC_MODE,
            .channel        = motor_cfg[i].ledc_channel,
            .timer_sel      = LEDC_TIMER,
            .intr_type      = LEDC_INTR_DISABLE,
            .gpio_num       = motor_cfg[i].pwm_pin,
            .duty           = 0,
            .hpoint         = 0
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_ch));
    }

    gpio_config(&io_conf);
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

    if (speed > 0.0f) {
        gpio_set_level(cfg->dir_a_pin, 1);
        gpio_set_level(cfg->dir_b_pin, 0);
    } else if (speed < 0.0f) {
        gpio_set_level(cfg->dir_a_pin, 0);
        gpio_set_level(cfg->dir_b_pin, 1);
    } else {
        gpio_set_level(cfg->dir_a_pin, 0);
        gpio_set_level(cfg->dir_b_pin, 0);
    }

    uint32_t duty = (uint32_t)((fabsf(speed) / 100.0f) * 1023.0f);
    ledc_set_duty(LEDC_MODE, cfg->ledc_channel, duty);
    ledc_update_duty(LEDC_MODE, cfg->ledc_channel);

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
