#include "telemetry_adc.h"
#include "pinout.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "TELEMETRY_ADC";

#define VOLTAGE_DIVIDER_RATIO 4.30f

static adc_oneshot_unit_handle_t adc_handle = NULL;
static const adc_channel_t batt_channel = ADC_CHANNEL_4; // GPIO 35 is ADC1_CH4

esp_err_t telemetry_adc_init(void) {
    ESP_LOGI(TAG, "Initializing Telemetry ADC1_CH4 (GPIO %d)...", GPIO_BATTERY_ADC);

    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_cfg, &adc_handle);
    if (ret != ESP_OK) return ret;

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    return adc_oneshot_config_channel(adc_handle, batt_channel, &chan_cfg);
}

esp_err_t telemetry_adc_read(float *battery_voltage, uint32_t *raw_mv) {
    if (!adc_handle) return ESP_ERR_INVALID_STATE;

    int raw_val = 0;
    esp_err_t ret = adc_oneshot_read(adc_handle, batt_channel, &raw_val);
    if (ret != ESP_OK) return ret;

    // 12-bit ADC -> 3300 mV max
    uint32_t mv = (uint32_t)(((float)raw_val / 4095.0f) * 3300.0f);
    if (raw_mv) *raw_mv = mv;

    if (battery_voltage) {
        float v_adc = (float)mv / 1000.0f;
        *battery_voltage = v_adc * VOLTAGE_DIVIDER_RATIO;
    }

    return ESP_OK;
}
