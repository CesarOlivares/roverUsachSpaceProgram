#include "pca9685_servo.h"
#include "pinout.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "PCA9685_SERVO";

#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      100000

#define REG_MODE1               0x00
#define REG_PRESCALE            0xFE
#define REG_LED0_ON_L           0x06

#define MODE1_RESTART           0x80
#define MODE1_SLEEP             0x10
#define MODE1_AI                0x20

static esp_err_t write_reg(uint8_t reg, uint8_t data) {
    uint8_t buf[2] = { reg, data };
    return i2c_master_write_to_device(I2C_MASTER_NUM, PCA9685_I2C_ADDR, buf, 2, pdMS_TO_TICKS(100));
}

static esp_err_t read_reg(uint8_t reg, uint8_t *data) {
    return i2c_master_write_read_device(I2C_MASTER_NUM, PCA9685_I2C_ADDR, &reg, 1, data, 1, pdMS_TO_TICKS(100));
}

esp_err_t pca9685_servo_init(void) {
    ESP_LOGI(TAG, "Initializing PCA9685 I2C Servo Expander (SDA: %d, SCL: %d)...", GPIO_I2C_SDA, GPIO_I2C_SCL);

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

    write_reg(REG_MODE1, MODE1_RESTART);
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t old_mode = 0;
    read_reg(REG_MODE1, &old_mode);
    uint8_t sleep_mode = (old_mode & 0x7F) | MODE1_SLEEP;

    write_reg(REG_MODE1, sleep_mode);
    write_reg(REG_PRESCALE, 121); // 50Hz Prescaler
    write_reg(REG_MODE1, old_mode);
    vTaskDelay(pdMS_TO_TICKS(5));
    write_reg(REG_MODE1, old_mode | MODE1_RESTART | MODE1_AI);

    ESP_LOGI(TAG, "PCA9685 Servo Driver Initialized.");
    return ESP_OK;
}

esp_err_t pca9685_set_angle(uint8_t channel, float angle) {
    if (channel >= PCA9685_CHANNELS) return ESP_ERR_INVALID_ARG;
    if (angle < 0.0f) angle = 0.0f;
    if (angle > 180.0f) angle = 180.0f;

    // 500us to 2500us mapping over 20ms period (4096 ticks)
    uint32_t pulse_us = (uint32_t)(500 + (angle / 180.0f) * 2000);
    uint32_t ticks = (pulse_us * 4096) / 20000;
    if (ticks > 4095) ticks = 4095;

    uint8_t reg_base = REG_LED0_ON_L + 4 * channel;
    uint8_t packet[5] = {
        reg_base,
        0, 0,
        (uint8_t)(ticks & 0xFF),
        (uint8_t)((ticks >> 8) & 0xFF)
    };

    return i2c_master_write_to_device(I2C_MASTER_NUM, PCA9685_I2C_ADDR, packet, sizeof(packet), pdMS_TO_TICKS(100));
}
