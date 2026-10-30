#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/*
 * Traccion con un puente H BTS7960 (IBT-2) que mueve todas las ruedas a la vez.
 *
 * La consigna no se aplica de golpe: una tarea propia acerca la salida real a
 * la pedida a cfg::kTractionRampPctPerS, para no hundir la fuente con el
 * arranque de los 6 motores (medida tomada de la version Arduino que
 * funcionaba). stopNow() es la excepcion: corta en seco.
 *
 * Es thread-safe: lo usan el controlador y su propia tarea de rampa.
 */
class TractionDriver {
public:
    /** Configura el PWM, el pin EN (si hay) y lanza la tarea de rampa. Idempotente. */
    bool begin();

    /**
     * Fija la consigna; la rampa la alcanza poco a poco.
     * @param speed -100 (atras a tope) .. 100 (adelante a tope); se acota.
     * @return la consigna aceptada tras acotar, o NAN si el valor no vale.
     */
    float setTarget(float speed);

    /** Corte inmediato, sin rampa: parada de emergencia, DISARM y failsafe. */
    void stopNow();

private:
    static void task(void *arg);
    void        loop();
    void        writeOutput(float speed);   // con mtx_ tomado

    SemaphoreHandle_t mtx_     = nullptr;
    float             target_  = 0.0f;
    float             current_ = 0.0f;
};
