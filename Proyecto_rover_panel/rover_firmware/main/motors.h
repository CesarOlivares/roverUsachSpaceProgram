#ifndef MOTORS_H
#define MOTORS_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NUM_MOTORS 6

typedef enum {
    MOTOR_FL = 0, // Motor 1: Front Left
    MOTOR_FR = 1, // Motor 2: Front Right
    MOTOR_ML = 2, // Motor 3: Middle Left
    MOTOR_MR = 3, // Motor 4: Middle Right
    MOTOR_RL = 4, // Motor 5: Rear Left
    MOTOR_RR = 5  // Motor 6: Rear Right
} motor_id_t;

/**
 * @brief Initialize all 6 DC motors with PWM timers and direction GPIO pins.
 * @return esp_err_t ESP_OK on success
 */
esp_err_t motors_init(void);

/**
 * @brief Set speed and direction for an individual motor.
 * @param motor Motor identifier (0 to 5)
 * @param speed Duty cycle float value from -100.0f (full reverse) to +100.0f (full forward). 0.0f = stop.
 * @return esp_err_t ESP_OK on success
 */
esp_err_t motor_set_speed(motor_id_t motor, float speed);

/**
 * @brief Set speeds for all 6 motors simultaneously.
 * @param speeds Array of 6 float speed values [-100.0f, +100.0f]
 */
void motors_set_all(const float speeds[NUM_MOTORS]);

/**
 * @brief Apply differential drive kinematic mixing.
 * @param linear Forward/backward speed percentage (-100.0 to 100.0)
 * @param angular Steering speed percentage (-100.0 to 100.0)
 */
void motors_drive_differential(float linear, float angular);

/**
 * @brief Emergency stop for all 6 motors (active brake).
 */
void motors_stop_all(void);

#ifdef __cplusplus
}
#endif

#endif // MOTORS_H
