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
 * @brief Configura el timer LEDC, los 6 canales PWM y los 6 pines de sentido.
 *
 * El enlace canal LEDC <-> GPIO se hace una sola vez aqui. En caliente solo se
 * escribe duty, de modo que la matriz GPIO nunca se reconfigura.
 * Es idempotente: llamarla dos veces no hace nada la segunda vez.
 */
esp_err_t mcpwm_motors_init(void);

/**
 * @brief Fija velocidad y sentido de un motor individual (M1..M6).
 * @param motor_index 0..5 (0 = M1 Frontal Izq, 1 = M2 Frontal Der, ...)
 * @param speed Porcentaje -100.0f (reversa plena) a +100.0f (avance pleno)
 */
esp_err_t mcpwm_motor_set_speed(int motor_index, float speed);

/**
 * @brief Mezcla cinematica diferencial y la aplica a los 6 motores de forma atomica.
 * @param linear  Traccion adelante/atras (-100.0f .. 100.0f)
 * @param angular Guinada / giro (-100.0f .. 100.0f)
 */
void mcpwm_motors_drive_differential(float linear, float angular);

/**
 * @brief Lleva los 6 motores a 0 % (rueda libre).
 */
void mcpwm_motors_stop_all(void);

/**
 * @brief Indica si algun motor tiene una consigna distinta de cero.
 *
 * Lo usa el failsafe para detectar hardware en movimiento aunque el rover
 * este desarmado (por ejemplo durante un TEST_MOTOR).
 */
bool mcpwm_motors_any_active(void);

/**
 * @brief Copia las consignas actuales de los 6 motores.
 * @param speeds Arreglo de 6 float a rellenar
 */
void mcpwm_motors_get_speeds(float speeds[NUM_MOTORS]);

#ifdef __cplusplus
}
#endif

#endif // MCPWM_MOTORS_H
