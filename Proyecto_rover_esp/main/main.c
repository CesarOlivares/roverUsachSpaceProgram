/*
 * Rover 6x6 - ESP32-S3 (N16R8)
 *
 * 6 motores DC sobre 3 puentes L298N, 5 servos de direccion por PCA9685,
 * un paso a paso DRV8825 y lectura de bateria por ADC1.
 * El mando y la telemetria van por rover_link (UART0 o WebSocket).
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "mcpwm_motors.h"
#include "stepper_drv.h"
#include "pca9685_servo.h"
#include "telemetry_adc.h"
#include "rover_link.h"

static const char *TAG = "ROVER";

void app_main(void)
{
    ESP_LOGI(TAG, "Rover 6x6 - ESP32-S3 arrancando (%s)",
             ROVER_LINK_WIRED ? "enlace por cable" : "enlace Wi-Fi");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // El orden importa: los actuadores tienen que estar en estado seguro antes
    // de que el enlace empiece a aceptar comandos.
    ESP_ERROR_CHECK(mcpwm_motors_init());
    ESP_ERROR_CHECK(stepper_drv_init());
    ESP_ERROR_CHECK(pca9685_servo_init());
    ESP_ERROR_CHECK(telemetry_adc_init());

    pca9685_center_all();

    ESP_ERROR_CHECK(rover_link_init());

    ESP_LOGI(TAG, "Subsistemas en linea.");

    // app_main puede terminar: las tareas del enlace, del stepper y el failsafe
    // ya corren por su cuenta. Mantenerla viva en un bucle de espera solo
    // ocuparia su pila de 3.5 KB sin hacer nada.
    vTaskDelete(NULL);
}
