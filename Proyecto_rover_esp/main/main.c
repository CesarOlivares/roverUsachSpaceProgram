#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "pinout.h"
#include "mcpwm_motors.h"
#include "stepper_drv.h"
#include "pca9685_servo.h"
#include "telemetry_adc.h"
#include "websocket_server.h"

static const char *TAG = "ROVER_MAIN";

void app_main(void) {
    ESP_LOGI(TAG, "=========================================================");
    ESP_LOGI(TAG, "   ESP32-S3 6x6 ROVER WEBSOCKET FIRMWARE (ESP-IDF v5.x)   ");
    ESP_LOGI(TAG, "=========================================================");

    // 1. Initialize NVS Flash
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize Hardware Subsystems
    ESP_ERROR_CHECK(mcpwm_motors_init());
    ESP_ERROR_CHECK(stepper_drv_init());
    ESP_ERROR_CHECK(pca9685_servo_init());
    ESP_ERROR_CHECK(telemetry_adc_init());

    // 3. Start SoftAP & WebSocket Server (Port 8080)
    ESP_ERROR_CHECK(websocket_server_init());

    ESP_LOGI(TAG, "Rover Subsystems and WebSocket Server Online.");

    while (1) {
        bool armed = websocket_is_armed();
        ESP_LOGD(TAG, "Heartbeat Loop | Rover State: %s", armed ? "ARMED" : "DISARMED");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
