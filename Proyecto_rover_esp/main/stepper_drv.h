#ifndef STEPPER_DRV_H
#define STEPPER_DRV_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configura los GPIO del DRV8825 y lanza la tarea de pasos (nucleo 1).
 *
 * El driver arranca deshabilitado. Es idempotente.
 */
esp_err_t stepper_drv_init(void);

/**
 * @brief Desplaza @p steps pasos relativos a la consigna actual.
 * @param speed_hz Pasos por segundo, acotado a 50..4000. 0 usa 1000 Hz.
 */
void stepper_drv_jog(int steps, uint32_t speed_hz);

/**
 * @brief Fija una consigna de posicion absoluta en pasos.
 * @param speed_hz Pasos por segundo, acotado a 50..4000. 0 usa 1000 Hz.
 */
void stepper_drv_move_to(int32_t target_position, uint32_t speed_hz);

/**
 * @brief Habilita o deshabilita la etapa de potencia. Al deshabilitar,
 *        descarta la consigna pendiente para que no salte al rehabilitar.
 */
void stepper_drv_set_enable(bool enable);

/** @brief Posicion actual en pasos. */
int32_t stepper_drv_get_position(void);

/** @brief true si el driver esta habilitado y aun no alcanza la consigna. */
bool stepper_drv_is_moving(void);

/**
 * @brief Parada inmediata: iguala consigna a posicion y corta el ENABLE.
 */
void stepper_drv_stop(void);

#ifdef __cplusplus
}
#endif

#endif // STEPPER_DRV_H
