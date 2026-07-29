#ifndef STEPPER_H
#define STEPPER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t current_position;
    int32_t target_position;
    uint32_t step_delay_us;
    bool is_moving;
    bool enabled;
} stepper_status_t;

/**
 * @brief Initialize the DRV8825 stepper driver and start its dedicated FreeRTOS task.
 * @return esp_err_t ESP_OK on success
 */
esp_err_t stepper_init(void);

/**
 * @brief Move stepper relative steps.
 * @param steps Number of steps (positive = forward, negative = reverse)
 * @param speed_hz Pulse frequency in steps per second (Hz)
 */
void stepper_move_steps(int32_t steps, uint32_t speed_hz);

/**
 * @brief Move stepper to an absolute target position.
 * @param target_pos Absolute target step index
 * @param speed_hz Pulse frequency in steps per second (Hz)
 */
void stepper_move_to(int32_t target_pos, uint32_t speed_hz);

/**
 * @brief Enable or disable motor holding torque (active low EN pin).
 * @param enable true to enable driver, false to disable/sleep
 */
void stepper_set_enable(bool enable);

/**
 * @brief Get current stepper motor position and movement state.
 * @param status Pointer to status struct to populate
 */
void stepper_get_status(stepper_status_t *status);

/**
 * @brief Stop active stepper movement immediately.
 */
void stepper_stop(void);

#ifdef __cplusplus
}
#endif

#endif // STEPPER_H
