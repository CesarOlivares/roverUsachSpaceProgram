#include "stepper_drv.h"
#include "pinout.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "STEPPER";

#define STEP_PULSE_US       5           // DRV8825 exige >=1.9 us de pulso alto
#define MIN_SPEED_HZ        50
#define MAX_SPEED_HZ        4000        // por encima el lazo por software no da
#define DEFAULT_SPEED_HZ    1000
#define IDLE_POLL_MS        10

typedef struct {
    int32_t  current_pos;
    int32_t  target_pos;
    uint32_t step_period_us;
    bool     enabled;
} stepper_state_t;

static stepper_state_t   g_st  = { 0, 0, 1000000 / DEFAULT_SPEED_HZ, false };
static SemaphoreHandle_t g_mtx = NULL;

// Escribe el pin ENABLE del DRV8825 (activo en bajo).
static inline void drv_enable_pin(bool enable)
{
    gpio_set_level(GPIO_STEPPER_EN, enable ? 0 : 1);
}

static void stepper_worker_task(void *arg)
{
    (void)arg;

    while (1) {
        int32_t  curr, target;
        uint32_t period_us;
        bool     enabled;

        xSemaphoreTake(g_mtx, portMAX_DELAY);
        curr      = g_st.current_pos;
        target    = g_st.target_pos;
        period_us = g_st.step_period_us;
        enabled   = g_st.enabled;
        xSemaphoreGive(g_mtx);

        if (!enabled || curr == target) {
            vTaskDelay(pdMS_TO_TICKS(IDLE_POLL_MS));
            continue;
        }

        bool dir_forward = (target > curr);
        gpio_set_level(GPIO_STEPPER_DIR, dir_forward ? 1 : 0);
        esp_rom_delay_us(2);                    // setup de DIR antes del flanco de STEP

        gpio_set_level(GPIO_STEPPER_STEP, 1);
        esp_rom_delay_us(STEP_PULSE_US);
        gpio_set_level(GPIO_STEPPER_STEP, 0);

        xSemaphoreTake(g_mtx, portMAX_DELAY);
        g_st.current_pos += dir_forward ? 1 : -1;
        bool arrived = (g_st.current_pos == g_st.target_pos);
        xSemaphoreGive(g_mtx);

        if (arrived) {
            continue;                           // el proximo ciclo entra en reposo
        }

        /*
         * Espera entre pasos. Con CONFIG_FREERTOS_HZ=1000 un tick son 1 ms.
         * Por encima de 1 kHz el periodo cae por debajo del tick, asi que el
         * resto se completa con una espera activa corta. Se cede la CPU al
         * menos una vez por paso para no dejar sin correr a la tarea idle del
         * nucleo 1 (lo que dispararia el task watchdog).
         */
        uint32_t whole_ms = period_us / 1000U;
        uint32_t rest_us  = period_us % 1000U;

        if (whole_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(whole_ms));
            if (rest_us > 0) esp_rom_delay_us(rest_us);
        } else {
            esp_rom_delay_us(period_us);
            taskYIELD();
        }
    }
}

esp_err_t stepper_drv_init(void)
{
    if (g_mtx) return ESP_OK;

    ESP_LOGI(TAG, "Inicializando DRV8825 (STEP %d, DIR %d, EN %d)...",
             GPIO_STEPPER_STEP, GPIO_STEPPER_DIR, GPIO_STEPPER_EN);

    g_mtx = xSemaphoreCreateMutex();
    if (!g_mtx) return ESP_ERR_NO_MEM;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_STEPPER_STEP) |
                        (1ULL << GPIO_STEPPER_DIR)  |
                        (1ULL << GPIO_STEPPER_EN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    gpio_set_level(GPIO_STEPPER_STEP, 0);
    gpio_set_level(GPIO_STEPPER_DIR, 0);
    drv_enable_pin(false);                      // arranca deshabilitado, sin consumo

    BaseType_t ok = xTaskCreatePinnedToCore(stepper_worker_task, "stepper_worker",
                                            3072, NULL, 5, NULL, 1);
    return (ok == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

// Normaliza y convierte a periodo. Devuelve microsegundos por paso.
static uint32_t speed_to_period_us(uint32_t speed_hz)
{
    if (speed_hz == 0)           speed_hz = DEFAULT_SPEED_HZ;
    if (speed_hz < MIN_SPEED_HZ) speed_hz = MIN_SPEED_HZ;
    if (speed_hz > MAX_SPEED_HZ) speed_hz = MAX_SPEED_HZ;
    return 1000000U / speed_hz;
}

void stepper_drv_jog(int steps, uint32_t speed_hz)
{
    if (!g_mtx || steps == 0) return;

    uint32_t period_us = speed_to_period_us(speed_hz);

    xSemaphoreTake(g_mtx, portMAX_DELAY);
    g_st.target_pos    += steps;
    g_st.step_period_us = period_us;
    g_st.enabled        = true;
    drv_enable_pin(true);
    xSemaphoreGive(g_mtx);
}

void stepper_drv_move_to(int32_t target_position, uint32_t speed_hz)
{
    if (!g_mtx) return;

    uint32_t period_us = speed_to_period_us(speed_hz);

    xSemaphoreTake(g_mtx, portMAX_DELAY);
    g_st.target_pos     = target_position;
    g_st.step_period_us = period_us;
    g_st.enabled        = true;
    drv_enable_pin(true);
    xSemaphoreGive(g_mtx);
}

void stepper_drv_set_enable(bool enable)
{
    if (!g_mtx) return;

    xSemaphoreTake(g_mtx, portMAX_DELAY);
    g_st.enabled = enable;
    if (!enable) {
        g_st.target_pos = g_st.current_pos;     // sin consigna pendiente al reactivar
    }
    drv_enable_pin(enable);
    xSemaphoreGive(g_mtx);
}

int32_t stepper_drv_get_position(void)
{
    if (!g_mtx) return 0;

    xSemaphoreTake(g_mtx, portMAX_DELAY);
    int32_t pos = g_st.current_pos;
    xSemaphoreGive(g_mtx);
    return pos;
}

bool stepper_drv_is_moving(void)
{
    if (!g_mtx) return false;

    xSemaphoreTake(g_mtx, portMAX_DELAY);
    bool moving = g_st.enabled && (g_st.current_pos != g_st.target_pos);
    xSemaphoreGive(g_mtx);
    return moving;
}

void stepper_drv_stop(void)
{
    if (!g_mtx) return;

    xSemaphoreTake(g_mtx, portMAX_DELAY);
    g_st.target_pos = g_st.current_pos;
    g_st.enabled    = false;
    drv_enable_pin(false);                      // suelta el motor y corta el calentamiento
    xSemaphoreGive(g_mtx);
}
