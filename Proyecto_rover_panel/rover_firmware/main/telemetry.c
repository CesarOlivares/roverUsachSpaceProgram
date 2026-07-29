#include "telemetry.h"
#include "pinout.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "TELEMETRY";

// Voltage divider ratio: R1 = 33k, R2 = 10k -> V_in = V_adc * ((33 + 10) / 10) = V_adc * 4.3
#define VOLTAGE_DIVIDER_RATIO 4.30f

static adc_oneshot_unit_handle_t adc1_handle = NULL;
static const adc_channel_t battery_adc_channel = ADC_CHANNEL_4; // GPIO 35 is ADC1_CH4 on ESP32-S3

esp_err_t telemetry_init(void) {
    ESP_LOGI(TAG, "Initializing Battery ADC Telemetry on GPIO %d (ADC1 Channel 4)...", GPIO_BATTERY_ADC);

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc1_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize ADC1 unit: %s", esp_err_to_name(ret));
        return ret;
    }

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT, // 12-bit (0..4095)
        .atten = ADC_ATTEN_DB_12,         // 0 ~ 3.3V range
    };
    ret = adc_oneshot_config_channel(adc1_handle, battery_adc_channel, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to config ADC channel: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Telemetry ADC initialized successfully.");
    return ESP_OK;
}

esp_err_t telemetry_read(telemetry_data_t *data) {
    if (!data || !adc1_handle) return ESP_ERR_INVALID_STATE;

    int raw_val = 0;
    esp_err_t ret = adc_oneshot_read(adc1_handle, battery_adc_channel, &raw_val);
    if (ret != ESP_OK) return ret;

    data->raw_adc = (uint16_t)raw_val;

    // Convert raw ADC (12-bit, 3.3V max) to voltage
    float v_adc = ((float)raw_val / 4095.0f) * 3.3f;
    data->battery_voltage = v_adc * VOLTAGE_DIVIDER_RATIO;
    data->uptime_seconds = (uint32_t)(esp_timer_get_time() / 1000000ULL);

    return ESP_OK;
}
