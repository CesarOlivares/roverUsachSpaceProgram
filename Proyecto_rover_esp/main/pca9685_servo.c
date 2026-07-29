#include "pca9685_servo.h"
#include "pinout.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <math.h>

static const char *TAG = "PCA9685";

#define I2C_PORT            I2C_NUM_0
#define I2C_FREQ_HZ         400000          // 400 kHz: el bus debe aguantar 5 servos a 20 Hz
#define I2C_TIMEOUT_MS      50

#define REG_MODE1           0x00
#define REG_PRESCALE        0xFE
#define REG_LED0_ON_L       0x06

#define MODE1_ALLCALL       0x01
#define MODE1_SLEEP         0x10
#define MODE1_AI            0x20
#define MODE1_RESTART       0x80

// Prescaler para 50 Hz con el oscilador interno de 25 MHz:
// round(25e6 / (4096 * 50)) - 1 = 121
#define PRESCALE_50HZ       121

#define SERVO_MIN_US        500.0f
#define SERVO_MAX_US        2500.0f
#define PWM_PERIOD_US       20000.0f        // 50 Hz
#define PWM_TICKS           4096.0f

static i2c_master_bus_handle_t g_bus     = NULL;
static i2c_master_dev_handle_t g_dev     = NULL;
static SemaphoreHandle_t       g_i2c_mtx = NULL;

// Ultimo tick escrito por canal. Evita repetir tramas identicas en el bus:
// el lazo DRIVE llega a 20 Hz y casi siempre pide el mismo angulo.
static int32_t g_last_ticks[PCA9685_CHANNELS];

static esp_err_t write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(g_dev, buf, sizeof(buf), I2C_TIMEOUT_MS);
}

static esp_err_t read_reg(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(g_dev, &reg, 1, val, 1, I2C_TIMEOUT_MS);
}

esp_err_t pca9685_servo_init(void)
{
    if (g_dev) return ESP_OK;

    ESP_LOGI(TAG, "Inicializando expansor de servos PCA9685 (SDA %d, SCL %d)...",
             GPIO_I2C_SDA, GPIO_I2C_SCL);

    g_i2c_mtx = xSemaphoreCreateMutex();
    if (!g_i2c_mtx) return ESP_ERR_NO_MEM;

    for (int i = 0; i < PCA9685_CHANNELS; i++) {
        g_last_ticks[i] = -1;   // -1 = aun sin escribir
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port                     = I2C_PORT,
        .sda_io_num                   = GPIO_I2C_SDA,
        .scl_io_num                   = GPIO_I2C_SCL,
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t ret = i2c_new_master_bus(&bus_cfg, &g_bus);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo crear el bus I2C: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = PCA9685_I2C_ADDR,
        .scl_speed_hz    = I2C_FREQ_HZ,
    };
    ret = i2c_master_bus_add_device(g_bus, &dev_cfg, &g_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo registrar el PCA9685 en 0x%02X: %s",
                 PCA9685_I2C_ADDR, esp_err_to_name(ret));
        return ret;
    }

    if (i2c_master_probe(g_bus, PCA9685_I2C_ADDR, I2C_TIMEOUT_MS) != ESP_OK) {
        // No es fatal: el rover debe poder rodar aunque falte la placa de servos.
        ESP_LOGW(TAG, "El PCA9685 no responde en 0x%02X. Revisa alimentacion y cableado.",
                 PCA9685_I2C_ADDR);
    }

    /*
     * Secuencia de arranque segun hoja de datos (7.3.1.1): el prescaler solo
     * admite escritura con SLEEP activo, y tras despertar hay que esperar
     * >=500 us a que el oscilador arranque antes de emitir RESTART.
     */
    uint8_t mode1 = MODE1_ALLCALL;
    read_reg(REG_MODE1, &mode1);

    write_reg(REG_MODE1, (uint8_t)((mode1 & ~MODE1_RESTART) | MODE1_SLEEP));
    write_reg(REG_PRESCALE, PRESCALE_50HZ);
    write_reg(REG_MODE1, (uint8_t)((mode1 & ~MODE1_SLEEP) | MODE1_AI));
    vTaskDelay(pdMS_TO_TICKS(5));
    write_reg(REG_MODE1, (uint8_t)((mode1 & ~MODE1_SLEEP) | MODE1_AI | MODE1_RESTART));

    ESP_LOGI(TAG, "PCA9685 listo a 50 Hz (prescale %d).", PRESCALE_50HZ);
    return ESP_OK;
}

esp_err_t pca9685_set_angle(uint8_t channel, float angle)
{
    if (!g_dev) return ESP_ERR_INVALID_STATE;
    if (channel >= PCA9685_CHANNELS) return ESP_ERR_INVALID_ARG;
    if (isnan(angle)) return ESP_ERR_INVALID_ARG;

    if (angle < 0.0f)   angle = 0.0f;
    if (angle > 180.0f) angle = 180.0f;

    float pulse_us = SERVO_MIN_US + (angle / 180.0f) * (SERVO_MAX_US - SERVO_MIN_US);
    int32_t ticks  = (int32_t)((pulse_us * PWM_TICKS) / PWM_PERIOD_US + 0.5f);
    if (ticks < 0)    ticks = 0;
    if (ticks > 4095) ticks = 4095;

    esp_err_t ret = ESP_OK;

    xSemaphoreTake(g_i2c_mtx, portMAX_DELAY);
    if (g_last_ticks[channel] != ticks) {
        // Auto-increment activo: ON_L, ON_H, OFF_L, OFF_H en una sola trama.
        uint8_t packet[5] = {
            (uint8_t)(REG_LED0_ON_L + 4 * channel),
            0x00,
            0x00,
            (uint8_t)(ticks & 0xFF),
            (uint8_t)((ticks >> 8) & 0x0F),
        };
        ret = i2c_master_transmit(g_dev, packet, sizeof(packet), I2C_TIMEOUT_MS);
        if (ret == ESP_OK) {
            g_last_ticks[channel] = ticks;
        }
    }
    xSemaphoreGive(g_i2c_mtx);

    return ret;
}

esp_err_t pca9685_center_all(void)
{
    esp_err_t last = ESP_OK;
    for (uint8_t ch = 0; ch < PCA9685_STEERING_CHANNELS; ch++) {
        esp_err_t ret = pca9685_set_angle(ch, 90.0f);
        if (ret != ESP_OK) last = ret;
    }
    return last;
}
