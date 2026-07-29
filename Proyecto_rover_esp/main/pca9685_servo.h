#ifndef PCA9685_SERVO_H
#define PCA9685_SERVO_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCA9685_CHANNELS 16

esp_err_t pca9685_servo_init(void);
esp_err_t pca9685_set_angle(uint8_t channel, float angle);

#ifdef __cplusplus
}
#endif

#endif // PCA9685_SERVO_H
