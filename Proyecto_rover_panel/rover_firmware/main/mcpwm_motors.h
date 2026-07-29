#ifndef MCPWM_MOTORS_H
#define MCPWM_MOTORS_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NUM_MOTORS 6

/**
 * @brief Initialize LEDC PWM and DIR GPIO pins for all 6 DC motors.
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mcpwm_motors_init(void);

/**
 * @brief Set speed and direction for an individual motor (M1..M6).
 * @param motor_index Index 0..5 (0 = M1 FL, 1 = M2 FR, etc.)
 * @param speed Duty percentage -100.0f (full reverse) to +100.0f (full forward)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t mcpwm_motor_set_speed(int motor_index, float speed);

/**
 * @brief Differential drive kinematic mixing.
 * @param linear Forward/backward throttle (-100.0f to 100.0f)
 * @param angular Steering yaw (-100.0f to 100.0f)
 */
void mcpwm_motors_drive_differential(float linear, float angular);

/**
 * @brief Active brake stop for all 6 motors.
 */
void mcpwm_motors_stop_all(void);

/**
 * @brief Retrieve current duty cycle percentages for all 6 motors.
 * @param speeds Array of 6 floats to populate
 */
void mcpwm_motors_get_speeds(float speeds[NUM_MOTORS]);

#ifdef __cplusplus
}
#endif

#endif // MCPWM_MOTORS_H
