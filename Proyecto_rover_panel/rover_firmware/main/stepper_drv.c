#include "stepper_drv.h"
#include "pinout.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "STEPPER_DRV";

static int32_t g_current_pos = 0;
static int32_t g_target_pos = 0;
static uint32_t g_step_delay_us = 1000;
static bool g_is_moving = false;
static bool g_enabled = false;
static SemaphoreHandle_t g_stepper_mutex = NULL;

static void stepper_worker_task(void *pvParameters) {
    while (1) {
        int32_t curr, target;
        uint32_t delay_us;
        bool enabled;

        xSemaphoreTake(g_stepper_mutex, portMAX_DELAY);
        curr = g_current_pos;
        target = g_target_pos;
        delay_us = g_step_delay_us;
        enabled = g_enabled;
        xSemaphoreGive(g_stepper_mutex);

        if (enabled && curr != target) {
            bool dir = (target > curr);
            gpio_set_level(GPIO_STEPPER_DIR, dir ? 1 : 0);

            gpio_set_level(GPIO_STEPPER_STEP, 1);
            esp_rom_delay_us(5);
            gpio_set_level(GPIO_STEPPER_STEP, 0);

            xSemaphoreTake(g_stepper_mutex, portMAX_DELAY);
            if (dir) g_current_pos++;
            else g_current_pos--;
            g_is_moving = (g_current_pos != g_target_pos);
            xSemaphoreGive(g_stepper_mutex);

            uint32_t wait_ticks = (delay_us / 1000) / portTICK_PERIOD_MS;
            if (wait_ticks > 0) {
                vTaskDelay(wait_ticks);
            } else {
                esp_rom_delay_us(delay_us);
            }
        } else {
            xSemaphoreTake(g_stepper_mutex, portMAX_DELAY);
            g_is_moving = false;
            xSemaphoreGive(g_stepper_mutex);

            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

esp_err_t stepper_drv_init(void) {
    ESP_LOGI(TAG, "Initializing DRV8825 Stepper...");
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

    gpio_set_level(GPIO_STEPPER_EN, 1); // Disabled by default
    gpio_set_level(GPIO_STEPPER_STEP, 0);
    gpio_set_level(GPIO_STEPPER_DIR, 0);

    xTaskCreatePinnedToCore(stepper_worker_task, "stepper_worker", 3072, NULL, 5, NULL, 1);
    return ESP_OK;
}

void stepper_drv_jog(int steps, uint32_t speed_hz) {
    if (speed_hz == 0) speed_hz = 1000;
    uint32_t delay_us = 1000000 / speed_hz;

    xSemaphoreTake(g_stepper_mutex, portMAX_DELAY);
    g_target_pos += steps;
    g_step_delay_us = delay_us;
    g_is_moving = true;
    stepper_drv_set_enable(true);
    xSemaphoreGive(g_stepper_mutex);
}

void stepper_drv_move_to(int32_t target_position, uint32_t speed_hz) {
    if (speed_hz == 0) speed_hz = 1000;
    uint32_t delay_us = 1000000 / speed_hz;

    xSemaphoreTake(g_stepper_mutex, portMAX_DELAY);
    g_target_pos = target_position;
    g_step_delay_us = delay_us;
    g_is_moving = true;
    stepper_drv_set_enable(true);
    xSemaphoreGive(g_stepper_mutex);
}

void stepper_drv_set_enable(bool enable) {
    gpio_set_level(GPIO_STEPPER_EN, enable ? 0 : 1);
    g_enabled = enable;
}

int32_t stepper_drv_get_position(void) {
    int32_t pos = 0;
    if (g_stepper_mutex) {
        xSemaphoreTake(g_stepper_mutex, portMAX_DELAY);
        pos = g_current_pos;
        xSemaphoreGive(g_stepper_mutex);
    }
    return pos;
}

bool stepper_drv_is_moving(void) {
    bool moving = false;
    if (g_stepper_mutex) {
        xSemaphoreTake(g_stepper_mutex, portMAX_DELAY);
        moving = g_is_moving;
        xSemaphoreGive(g_stepper_mutex);
    }
    return moving;
}

void stepper_drv_stop(void) {
    xSemaphoreTake(g_stepper_mutex, portMAX_DELAY);
    g_target_pos = g_current_pos;
    g_is_moving = false;
    xSemaphoreGive(g_stepper_mutex);
}
