#ifndef TELEMETRY_ADC_H
#define TELEMETRY_ADC_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configura ADC1 canal 8 (GPIO 9) y su curva de calibracion.
 *        Es idempotente.
 */
esp_err_t telemetry_adc_init(void);

/**
 * @brief Lee la bateria.
 * @param battery_voltage Voltaje de bateria en V, ya compensado por el divisor
 *                        resistivo (admite NULL)
 * @param raw_mv          Milivoltios medidos en el pin, antes del divisor
 *                        (admite NULL)
 */
esp_err_t telemetry_adc_read(float *battery_voltage, uint32_t *raw_mv);

#ifdef __cplusplus
}
#endif

#endif // TELEMETRY_ADC_H
