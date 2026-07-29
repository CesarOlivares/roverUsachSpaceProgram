#ifndef PCA9685_H
#define PCA9685_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCA9685_CHANNELS 16

/**
 * @brief Initialize native ESP-IDF I2C master and setup PCA9685 at 50Hz for RC servos.
 * @return esp_err_t ESP_OK on success
 */
esp_err_t pca9685_init(void);

/**
 * @brief Set raw ON/OFF PWM counts for a PCA9685 channel.
 * @param channel Servo channel (0..15)
 * @param on_count Tick count to turn signal HIGH (0..4095)
 * @param off_count Tick count to turn signal LOW (0..4095)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t pca9685_set_pwm(uint8_t channel, uint16_t on_count, uint16_t off_count);

/**
 * @brief Set servo angle for a channel (0° to 180°).
 * @param channel Servo channel (0..15)
 * @param angle Angle in degrees (0.0 to 180.0)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t pca9685_set_servo_angle(uint8_t channel, float angle);

/**
 * @brief Set PWM pulse length in microseconds for a channel.
 * @param channel Servo channel (0..15)
 * @param pulse_us Pulse width in microseconds (e.g. 500us to 2500us)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t pca9685_set_pwm_us(uint8_t channel, uint16_t pulse_us);

#ifdef __cplusplus
}
#endif

#endif // PCA9685_H
