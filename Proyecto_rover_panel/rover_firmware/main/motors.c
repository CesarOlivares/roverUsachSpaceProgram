#include "motors.h"
#include "pinout.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "MOTORS";

// LEDC configuration for 20 kHz motor PWM
#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_DUTY_RES           LEDC_TIMER_10_BIT // 0..1023
#define LEDC_FREQUENCY          20000             // 20 kHz PWM for DC motor drivers

typedef struct {
    int pwm_pin;
    int dir_a_pin;
    int dir_b_pin;
    ledc_channel_t ledc_channel;
} motor_config_t;

static const motor_config_t motor_configs[NUM_MOTORS] = {
    { GPIO_M1_PWM, GPIO_M1_DIR_A, GPIO_M1_DIR_B, LEDC_CHANNEL_0 }, // M1: FL
    { GPIO_M2_PWM, GPIO_M2_DIR_A, GPIO_M2_DIR_B, LEDC_CHANNEL_1 }, // M2: FR
    { GPIO_M3_PWM, GPIO_M3_DIR_A, GPIO_M3_DIR_B, LEDC_CHANNEL_2 }, // M3: ML
    { GPIO_M4_PWM, GPIO_M4_DIR_A, GPIO_M4_DIR_B, LEDC_CHANNEL_3 }, // M4: MR
    { GPIO_M5_PWM, GPIO_M5_DIR_A, GPIO_M5_DIR_B, LEDC_CHANNEL_4 }, // M5: RL
    { GPIO_M6_PWM, GPIO_M6_DIR_A, GPIO_M6_DIR_B, LEDC_CHANNEL_5 }  // M6: RR
};

esp_err_t motors_init(void) {
    ESP_LOGI(TAG, "Initializing 6x6 DC Motor Drivers (TB6612FNG)...");

    // 1. Configure LEDC Timer
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = LEDC_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // 2. Configure Direction GPIOs and LEDC Channels
    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 0,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    for (int i = 0; i < NUM_MOTORS; i++) {
        // Add DIR_A and DIR_B to mask
        io_conf.pin_bit_mask |= (1ULL << motor_configs[i].dir_a_pin);
        io_conf.pin_bit_mask |= (1ULL << motor_configs[i].dir_b_pin);

        // Configure LEDC channel for PWM pin
        ledc_channel_config_t ledc_channel = {
            .speed_mode     = LEDC_MODE,
            .channel        = motor_configs[i].ledc_channel,
            .timer_sel      = LEDC_TIMER,
            .intr_type      = LEDC_INTR_DISABLE,
            .gpio_num       = motor_configs[i].pwm_pin,
            .duty           = 0,
            .hpoint         = 0
        };
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
    }

    gpio_config(&io_conf);

    // Initial stop
    motors_stop_all();
    ESP_LOGI(TAG, "Motors initialized successfully.");
    return ESP_OK;
}

esp_err_t motor_set_speed(motor_id_t motor, float speed) {
    if (motor < 0 || motor >= NUM_MOTORS) return ESP_ERR_INVALID_ARG;

    // Clamp speed to [-100.0, 100.0]
    if (speed > 100.0f) speed = 100.0f;
    if (speed < -100.0f) speed = -100.0f;

    const motor_config_t *cfg = &motor_configs[motor];

    // Set directions (TB6612FNG truth table)
    if (speed > 0.0f) {
        gpio_set_level(cfg->dir_a_pin, 1);
        gpio_set_level(cfg->dir_b_pin, 0);
    } else if (speed < 0.0f) {
        gpio_set_level(cfg->dir_a_pin, 0);
        gpio_set_level(cfg->dir_b_pin, 1);
    } else {
        // Stop / Brake
        gpio_set_level(cfg->dir_a_pin, 0);
        gpio_set_level(cfg->dir_b_pin, 0);
    }

    // Convert duty percentage (0..100) to 10-bit resolution (0..1023)
    uint32_t duty = (uint32_t)((fabsf(speed) / 100.0f) * 1023.0f);
    ledc_set_duty(LEDC_MODE, cfg->ledc_channel, duty);
    ledc_update_duty(LEDC_MODE, cfg->ledc_channel);

    return ESP_OK;
}

void motors_set_all(const float speeds[NUM_MOTORS]) {
    for (int i = 0; i < NUM_MOTORS; i++) {
        motor_set_speed((motor_id_t)i, speeds[i]);
    }
}

void motors_drive_differential(float linear, float angular) {
    // Standard Skid Steering Kinematic Model for 6x6 Rover
    float left_speed  = linear + angular;
    float right_speed = linear - angular;

    // Normalize speeds if any exceeds 100%
    float max_val = fabsf(left_speed);
    if (fabsf(right_speed) > max_val) max_val = fabsf(right_speed);
    if (max_val > 100.0f) {
        left_speed = (left_speed / max_val) * 100.0f;
        right_speed = (right_speed / max_val) * 100.0f;
    }

    // Left side: FL, ML, RL
    motor_set_speed(MOTOR_FL, left_speed);
    motor_set_speed(MOTOR_ML, left_speed);
    motor_set_speed(MOTOR_RL, left_speed);

    // Right side: FR, MR, RR
    motor_set_speed(MOTOR_FR, right_speed);
    motor_set_speed(MOTOR_MR, right_speed);
    motor_set_speed(MOTOR_RR, right_speed);
}

void motors_stop_all(void) {
    for (int i = 0; i < NUM_MOTORS; i++) {
        motor_set_speed((motor_id_t)i, 0.0f);
    }
}
