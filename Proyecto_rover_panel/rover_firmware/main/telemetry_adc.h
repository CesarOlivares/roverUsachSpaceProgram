#ifndef TELEMETRY_ADC_H
#define TELEMETRY_ADC_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t telemetry_adc_init(void);
esp_err_t telemetry_adc_read(float *battery_voltage, uint32_t *raw_mv);

#ifdef __cplusplus
}
#endif

#endif // TELEMETRY_ADC_H
