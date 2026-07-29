#ifndef PCA9685_SERVO_H
#define PCA9685_SERVO_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Canales fisicos del integrado.
#define PCA9685_CHANNELS 16

// Canales realmente cableados a los servos de direccion del rover (0..4).
#define PCA9685_STEERING_CHANNELS 5

/**
 * @brief Levanta el bus I2C maestro y deja el PCA9685 a 50 Hz.
 *
 * No falla si la placa no responde: registra un aviso y devuelve ESP_OK para
 * que el rover pueda rodar sin direccion asistida. Es idempotente.
 */
esp_err_t pca9685_servo_init(void);

/**
 * @brief Posiciona un servo. Filtra escrituras repetidas para no saturar el bus.
 * @param channel 0..15
 * @param angle   0.0f .. 180.0f grados
 */
esp_err_t pca9685_set_angle(uint8_t channel, float angle);

/**
 * @brief Lleva los 5 servos de direccion a 90 grados (centro).
 */
esp_err_t pca9685_center_all(void);

#ifdef __cplusplus
}
#endif

#endif // PCA9685_SERVO_H
