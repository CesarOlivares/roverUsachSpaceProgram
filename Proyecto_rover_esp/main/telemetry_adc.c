#include "telemetry_adc.h"
#include "pinout.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "ADC";

// Relacion del divisor resistivo de la bateria montado en la placa.
#define VOLTAGE_DIVIDER_RATIO   4.30f

// GPIO 9 corresponde a ADC1 canal 8 en el ESP32-S3.
#define BATT_ADC_UNIT           ADC_UNIT_1
#define BATT_ADC_CHANNEL        ADC_CHANNEL_8
#define BATT_ADC_ATTEN          ADC_ATTEN_DB_12

static adc_oneshot_unit_handle_t g_adc  = NULL;
static adc_cali_handle_t         g_cali = NULL;   // NULL si el chip no trae curva grabada

esp_err_t telemetry_adc_init(void)
{
    if (g_adc) return ESP_OK;

    ESP_LOGI(TAG, "Inicializando ADC1 canal %d (GPIO %d) para la bateria...",
             BATT_ADC_CHANNEL, GPIO_BATTERY_ADC);

    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id  = BATT_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&unit_cfg, &g_adc);
    if (ret != ESP_OK) return ret;

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = BATT_ADC_ATTEN,
    };
    ret = adc_oneshot_config_channel(g_adc, BATT_ADC_CHANNEL, &chan_cfg);
    if (ret != ESP_OK) return ret;

    /*
     * Calibracion por curva de fabrica. Sin ella habria que aproximar con una
     * regla de tres sobre 3300 mV, que en el ESP32-S3 se desvia facil un 5-10 %
     * por la no linealidad del ADC: suficiente para dar un voltaje de bateria
     * falso. Si el chip no trae eFuse de calibracion se cae al calculo simple.
     */
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = BATT_ADC_UNIT,
        .chan     = BATT_ADC_CHANNEL,
        .atten    = BATT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &g_cali) != ESP_OK) {
        g_cali = NULL;
        ESP_LOGW(TAG, "Sin calibracion de fabrica: el voltaje sera aproximado.");
    }

    return ESP_OK;
}

esp_err_t telemetry_adc_read(float *battery_voltage, uint32_t *raw_mv)
{
    if (!g_adc) return ESP_ERR_INVALID_STATE;

    int raw = 0;
    esp_err_t ret = adc_oneshot_read(g_adc, BATT_ADC_CHANNEL, &raw);
    if (ret != ESP_OK) return ret;

    int mv = 0;
    if (!g_cali || adc_cali_raw_to_voltage(g_cali, raw, &mv) != ESP_OK) {
        mv = (int)((raw / 4095.0f) * 3300.0f);
    }
    if (mv < 0) mv = 0;

    if (raw_mv)          *raw_mv          = (uint32_t)mv;
    if (battery_voltage) *battery_voltage = (mv / 1000.0f) * VOLTAGE_DIVIDER_RATIO;

    return ESP_OK;
}
