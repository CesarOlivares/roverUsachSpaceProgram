#ifndef STEPPER_DRV_H
#define STEPPER_DRV_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t stepper_drv_init(void);
void stepper_drv_jog(int steps, uint32_t speed_hz);
void stepper_drv_move_to(int32_t target_position, uint32_t speed_hz);
void stepper_drv_set_enable(bool enable);
int32_t stepper_drv_get_position(void);
bool stepper_drv_is_moving(void);
void stepper_drv_stop(void);

#ifdef __cplusplus
}
#endif

#endif // STEPPER_DRV_H
