#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/*
 * Paso a paso con DRV8825. Genera los pulsos desde una tarea propia en el
 * nucleo 1, asi que a diferencia de los otros drivers si lleva mutex interno.
 */
class StepperDriver {
public:
    /** Configura los GPIO y lanza la tarea de pasos. Arranca deshabilitado. Idempotente. */
    bool begin();

    /**
     * Desplaza @p steps pasos relativos a la consigna actual.
     * @param speedHz pasos por segundo, acotado a 50..4000. 0 usa 1000 Hz.
     */
    void jog(int32_t steps, uint32_t speedHz);

    /** Consigna de posicion absoluta en pasos. */
    void moveTo(int32_t target, uint32_t speedHz);

    /** Parada inmediata: iguala consigna a posicion y corta el ENABLE. */
    void stop();

    int32_t position();
    bool    isMoving();

private:
    static void workerTask(void *arg);
    void        workerLoop();
    static uint32_t speedToPeriodUs(uint32_t speedHz);
    static void     enablePin(bool enable);

    SemaphoreHandle_t mtx_       = nullptr;
    int32_t           current_   = 0;
    int32_t           target_    = 0;
    uint32_t          periodUs_  = 1000;
    bool              enabled_   = false;
};
