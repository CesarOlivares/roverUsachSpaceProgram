#include "pca9685.h"
#include "pinout.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "PCA9685";

#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      100000

// PCA9685 Registers
#define REG_MODE1               0x00
#define REG_MODE2               0x01
#define REG_PRESCALE            0xFE
#define REG_LED0_ON_L           0x06

// Bit defines
#define MODE1_RESTART           0x80
#define MODE1_SLEEP             0x10
#define MODE1_AI                0x20

static esp_err_t pca9685_write_reg(uint8_t reg, uint8_t data) {
    uint8_t write_buf[2] = { reg, data };
    return i2c_master_write_to_device(I2C_MASTER_NUM, PCA9685_I2C_ADDR, write_buf, sizeof(write_buf), pdMS_TO_TICKS(100));
}

static esp_err_t pca9685_read_reg(uint8_t reg, uint8_t *data) {
    return i2c_master_write_read_device(I2C_MASTER_NUM, PCA9685_I2C_ADDR, &reg, 1, data, 1, pdMS_TO_TICKS(100));
}

esp_err_t pca9685_init(void) {
    ESP_LOGI(TAG, "Initializing Native I2C Master on SDA: %d, SCL: %d...", GPIO_I2C_SDA, GPIO_I2C_SCL);

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = GPIO_I2C_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = GPIO_I2C_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
        .clk_flags = 0
    };

    esp_err_t ret = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (ret != ESP_OK) return ret;

    ret = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    // Reset PCA9685
    ESP_LOGI(TAG, "Configuring PCA9685 (Address: 0x%02X) to 50Hz...", PCA9685_I2C_ADDR);
    pca9685_write_reg(REG_MODE1, MODE1_RESTART);
    vTaskDelay(pdMS_TO_TICKS(10));

    // Set 50Hz PWM frequency for standard RC servos
    // Prescale = round(25MHz / (4096 * 50Hz)) - 1 = 121 (0x79)
    uint8_t old_mode = 0;
    pca9685_read_reg(REG_MODE1, &old_mode);
    uint8_t sleep_mode = (old_mode & 0x7F) | MODE1_SLEEP;

    pca9685_write_reg(REG_MODE1, sleep_mode);        // Go to sleep to set prescale
    pca9685_write_reg(REG_PRESCALE, 121);             // 50 Hz prescale
    pca9685_write_reg(REG_MODE1, old_mode);           // Wake up
    vTaskDelay(pdMS_TO_TICKS(5));
    pca9685_write_reg(REG_MODE1, old_mode | MODE1_RESTART | MODE1_AI); // Enable Auto-Increment

    ESP_LOGI(TAG, "PCA9685 initialized successfully.");
    return ESP_OK;
}

esp_err_t pca9685_set_pwm(uint8_t channel, uint16_t on_count, uint16_t off_count) {
    if (channel >= PCA9685_CHANNELS) return ESP_ERR_INVALID_ARG;

    uint8_t reg_base = REG_LED0_ON_L + 4 * channel;
    uint8_t packet[5] = {
        reg_base,
        (uint8_t)(on_count & 0xFF),
        (uint8_t)((on_count >> 8) & 0xFF),
        (uint8_t)(off_count & 0xFF),
        (uint8_t)((off_count >> 8) & 0xFF)
    };

    return i2c_master_write_to_device(I2C_MASTER_NUM, PCA9685_I2C_ADDR, packet, sizeof(packet), pdMS_TO_TICKS(100));
}

esp_err_t pca9685_set_pwm_us(uint8_t channel, uint16_t pulse_us) {
    // 50 Hz period = 20,000 us = 4096 ticks
    // 1 tick = 20000 / 4096 = 4.8828 us
    uint32_t ticks = (pulse_us * 4096) / 20000;
    if (ticks > 4095) ticks = 4095;
    return pca9685_set_pwm(channel, 0, (uint16_t)ticks);
}

esp_err_t pca9685_set_servo_angle(uint8_t channel, float angle) {
    if (angle < 0.0f) angle = 0.0f;
    if (angle > 180.0f) angle = 180.0f;

    // Standard servo pulse: 500us (0 deg) to 2500us (180 deg)
    uint16_t pulse_us = (uint16_t)(500 + (angle / 180.0f) * 2000);
    return pca9685_set_pwm_us(channel, pulse_us);
}
