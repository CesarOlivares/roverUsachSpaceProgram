#include "StepperDriver.h"

#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "Pinout.h"
#include "../util/Log.h"

#define STEP_PULSE_US     5         // DRV8825 exige >=1.9 us de pulso alto
#define MIN_SPEED_HZ      50
#define MAX_SPEED_HZ      4000      // por encima el lazo por software no da
#define DEFAULT_SPEED_HZ  1000
#define IDLE_POLL_MS      10

// Escribe el pin ENABLE del DRV8825 (activo en bajo).
void StepperDriver::enablePin(bool enable)
{
    gpio_set_level((gpio_num_t)pins::kStepperEn, enable ? 0 : 1);
}

uint32_t StepperDriver::speedToPeriodUs(uint32_t speedHz)
{
    if (speedHz == 0)            speedHz = DEFAULT_SPEED_HZ;
    if (speedHz < MIN_SPEED_HZ)  speedHz = MIN_SPEED_HZ;
    if (speedHz > MAX_SPEED_HZ)  speedHz = MAX_SPEED_HZ;
    return 1000000U / speedHz;
}

void StepperDriver::workerTask(void *arg)
{
    static_cast<StepperDriver *>(arg)->workerLoop();
}

void StepperDriver::workerLoop()
{
    while (true) {
        xSemaphoreTake(mtx_, portMAX_DELAY);
        int32_t  curr      = current_;
        int32_t  target    = target_;
        uint32_t period_us = periodUs_;
        bool     enabled   = enabled_;
        xSemaphoreGive(mtx_);

        if (!enabled || curr == target) {
            vTaskDelay(pdMS_TO_TICKS(IDLE_POLL_MS));
            continue;
        }

        bool forward = (target > curr);
        gpio_set_level((gpio_num_t)pins::kStepperDir, forward ? 1 : 0);
        esp_rom_delay_us(2);                    // setup de DIR antes del flanco de STEP

        gpio_set_level((gpio_num_t)pins::kStepperStep, 1);
        esp_rom_delay_us(STEP_PULSE_US);
        gpio_set_level((gpio_num_t)pins::kStepperStep, 0);

        xSemaphoreTake(mtx_, portMAX_DELAY);
        current_ += forward ? 1 : -1;
        bool arrived = (current_ == target_);
        xSemaphoreGive(mtx_);

        if (arrived) continue;                  // el proximo ciclo entra en reposo

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

bool StepperDriver::begin()
{
    if (mtx_) return true;

    Log::info("STEPPER", "Inicializando DRV8825 (STEP %d, DIR %d, EN %d)...",
              pins::kStepperStep, pins::kStepperDir, pins::kStepperEn);

    mtx_ = xSemaphoreCreateMutex();
    if (!mtx_) return false;

    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << pins::kStepperStep) |
                           (1ULL << pins::kStepperDir)  |
                           (1ULL << pins::kStepperEn);
    io_conf.mode         = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en   = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type    = GPIO_INTR_DISABLE;
    if (gpio_config(&io_conf) != ESP_OK) return false;

    gpio_set_level((gpio_num_t)pins::kStepperStep, 0);
    gpio_set_level((gpio_num_t)pins::kStepperDir, 0);
    enablePin(false);                           // arranca deshabilitado, sin consumo

    periodUs_ = speedToPeriodUs(DEFAULT_SPEED_HZ);

    BaseType_t ok = xTaskCreatePinnedToCore(workerTask, "stepper_worker",
                                            3072, this, 5, nullptr, 1);
    return ok == pdPASS;
}

void StepperDriver::jog(int32_t steps, uint32_t speedHz)
{
    if (!mtx_ || steps == 0) return;

    uint32_t period_us = speedToPeriodUs(speedHz);

    xSemaphoreTake(mtx_, portMAX_DELAY);
    // Suma en 64 bits y satura: una consigna acumulada no debe dar la vuelta.
    int64_t t = (int64_t)target_ + steps;
    if (t > INT32_MAX) t = INT32_MAX;
    if (t < INT32_MIN) t = INT32_MIN;
    target_   = (int32_t)t;
    periodUs_ = period_us;
    enabled_  = true;
    enablePin(true);
    xSemaphoreGive(mtx_);
}

void StepperDriver::moveTo(int32_t target, uint32_t speedHz)
{
    if (!mtx_) return;

    uint32_t period_us = speedToPeriodUs(speedHz);

    xSemaphoreTake(mtx_, portMAX_DELAY);
    target_   = target;
    periodUs_ = period_us;
    enabled_  = true;
    enablePin(true);
    xSemaphoreGive(mtx_);
}

void StepperDriver::stop()
{
    if (!mtx_) return;

    xSemaphoreTake(mtx_, portMAX_DELAY);
    target_  = current_;
    enabled_ = false;
    enablePin(false);                           // suelta el motor y corta el calentamiento
    xSemaphoreGive(mtx_);
}

int32_t StepperDriver::position()
{
    if (!mtx_) return 0;

    xSemaphoreTake(mtx_, portMAX_DELAY);
    int32_t pos = current_;
    xSemaphoreGive(mtx_);
    return pos;
}

bool StepperDriver::isMoving()
{
    if (!mtx_) return false;

    xSemaphoreTake(mtx_, portMAX_DELAY);
    bool moving = enabled_ && (current_ != target_);
    xSemaphoreGive(mtx_);
    return moving;
}
