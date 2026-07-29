#include "stepper.h"
#include "pinout.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "STEPPER";

static stepper_status_t g_stepper_status = {
    .current_position = 0,
    .target_position = 0,
    .step_delay_us = 1000, // Default 1kHz step rate
    .is_moving = false,
    .enabled = false
};

static SemaphoreHandle_t g_stepper_mutex = NULL;
static TaskHandle_t g_stepper_task_handle = NULL;

static void stepper_task(void *pvParameters) {
    ESP_LOGI(TAG, "Stepper FreeRTOS Task started.");

    while (1) {
        int32_t curr, target;
        uint32_t delay_us;
        bool is_enabled;

        xSemaphoreTake(g_stepper_mutex, portMAX_DELAY);
        curr = g_stepper_status.current_position;
        target = g_stepper_status.target_position;
        delay_us = g_stepper_status.step_delay_us;
        is_enabled = g_stepper_status.enabled;
        xSemaphoreGive(g_stepper_mutex);

        if (is_enabled && curr != target) {
            bool dir = (target > curr);
            
            // Set DIR pin level
            gpio_set_level(GPIO_STEPPER_DIR, dir ? 1 : 0);

            // Generate STEP pulse (5us pulse width)
            gpio_set_level(GPIO_STEPPER_STEP, 1);
            esp_rom_delay_us(5);
            gpio_set_level(GPIO_STEPPER_STEP, 0);

            // Update position
            xSemaphoreTake(g_stepper_mutex, portMAX_DELAY);
            if (dir) {
                g_stepper_status.current_position++;
            } else {
                g_stepper_status.current_position--;
            }
            g_stepper_status.is_moving = (g_stepper_status.current_position != g_stepper_status.target_position);
            xSemaphoreGive(g_stepper_mutex);

            // Delay between steps
            uint32_t wait_ticks = (delay_us / 1000) / portTICK_PERIOD_MS;
            if (wait_ticks > 0) {
                vTaskDelay(wait_ticks);
            } else {
                esp_rom_delay_us(delay_us);
            }
        } else {
            xSemaphoreTake(g_stepper_mutex, portMAX_DELAY);
            g_stepper_status.is_moving = false;
            xSemaphoreGive(g_stepper_mutex);

            vTaskDelay(pdMS_TO_TICKS(10)); // Idle poll
        }
    }
}

esp_err_t stepper_init(void) {
    ESP_LOGI(TAG, "Initializing DRV8825 Stepper Driver (STEP: %d, DIR: %d, EN: %d)...",
             GPIO_STEPPER_STEP, GPIO_STEPPER_DIR, GPIO_STEPPER_EN);

    g_stepper_mutex = xSemaphoreCreateMutex();
    if (!g_stepper_mutex) return ESP_ERR_NO_MEM;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_STEPPER_STEP) | (1ULL << GPIO_STEPPER_DIR) | (1ULL << GPIO_STEPPER_EN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    // Disable driver by default (EN pin HIGH on DRV8825)
    gpio_set_level(GPIO_STEPPER_EN, 1);
    gpio_set_level(GPIO_STEPPER_STEP, 0);
    gpio_set_level(GPIO_STEPPER_DIR, 0);

    // Spawn high-priority FreeRTOS Stepper task
    xTaskCreatePinnedToCore(stepper_task, "stepper_task", 3072, NULL, 5, &g_stepper_task_handle, 1);

    return ESP_OK;
}

void stepper_move_steps(int32_t steps, uint32_t speed_hz) {
    if (speed_hz == 0) speed_hz = 1000;
    uint32_t delay_us = 1000000 / speed_hz;

    xSemaphoreTake(g_stepper_mutex, portMAX_DELAY);
    g_stepper_status.target_position += steps;
    g_stepper_status.step_delay_us = delay_us;
    g_stepper_status.is_moving = true;
    stepper_set_enable(true);
    xSemaphoreGive(g_stepper_mutex);
}

void stepper_move_to(int32_t target_pos, uint32_t speed_hz) {
    if (speed_hz == 0) speed_hz = 1000;
    uint32_t delay_us = 1000000 / speed_hz;

    xSemaphoreTake(g_stepper_mutex, portMAX_DELAY);
    g_stepper_status.target_position = target_pos;
    g_stepper_status.step_delay_us = delay_us;
    g_stepper_status.is_moving = true;
    stepper_set_enable(true);
    xSemaphoreGive(g_stepper_mutex);
}

void stepper_set_enable(bool enable) {
    // Active LOW enable pin on DRV8825
    gpio_set_level(GPIO_STEPPER_EN, enable ? 0 : 1);
    g_stepper_status.enabled = enable;
}

void stepper_get_status(stepper_status_t *status) {
    if (status && g_stepper_mutex) {
        xSemaphoreTake(g_stepper_mutex, portMAX_DELAY);
        *status = g_stepper_status;
        xSemaphoreGive(g_stepper_mutex);
    }
}

void stepper_stop(void) {
    xSemaphoreTake(g_stepper_mutex, portMAX_DELAY);
    g_stepper_status.target_position = g_stepper_status.current_position;
    g_stepper_status.is_moving = false;
    xSemaphoreGive(g_stepper_mutex);
}
