#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float battery_voltage;  // Volts (e.g. 11.1V - 12.6V)
    uint16_t raw_adc;       // Raw ADC reading (0..4095)
    uint32_t uptime_seconds;// Rover uptime in seconds
} telemetry_data_t;

/**
 * @brief Initialize ADC1 for battery voltage sensing on GPIO 35.
 * @return esp_err_t ESP_OK on success
 */
esp_err_t telemetry_init(void);

/**
 * @brief Read battery voltage and update telemetry data structure.
 * @param data Pointer to telemetry_data_t struct to populate
 * @return esp_err_t ESP_OK on success
 */
esp_err_t telemetry_read(telemetry_data_t *data);

#ifdef __cplusplus
}
#endif

#endif // TELEMETRY_H
