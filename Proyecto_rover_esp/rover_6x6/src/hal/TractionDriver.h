#pragma once

#include <stdint.h>

/*
 * Traccion con un puente H BTS7960 (IBT-2) que mueve todas las ruedas a la vez.
 *
 * Solo sabe mover hardware: no guarda estado de negocio. No es thread-safe;
 * lo llama unicamente CommandController, que serializa los accesos.
 */
class TractionDriver {
public:
    /**
     * Configura el timer LEDC y los dos canales PWM (RPWM y LPWM).
     * El enlace canal LEDC <-> GPIO se hace una sola vez aqui. Es idempotente.
     */
    bool begin();

    /**
     * Fija velocidad y sentido.
     * @param speed -100 (atras a tope) .. 100 (adelante a tope); se acota.
     * @return la velocidad aplicada tras acotar, o NAN si el valor no vale.
     */
    float apply(float speed);

    /** Ambas entradas a 0: el BTS7960 frena con los dos lados bajos. */
    void stop();

private:
    bool initialized_ = false;
};
