#pragma once

#include <stdint.h>
#include "../config/RoverConfig.h"

/*
 * 6 motores DC sobre 3 puentes L298N, con PWM por LEDC a 20 kHz.
 *
 * Solo sabe mover hardware: no guarda estado de negocio. No es thread-safe;
 * lo llama unicamente CommandController, que serializa los accesos.
 */
class MotorDriver {
public:
    /**
     * Configura el timer LEDC, los 6 canales PWM y los 6 pines de sentido.
     * El enlace canal LEDC <-> GPIO se hace una sola vez aqui. Es idempotente.
     */
    bool begin();

    /**
     * Fija velocidad y sentido de un motor (0 = M1 ... 5 = M6).
     * @param speed -100 (reversa plena) .. 100 (avance pleno); se acota.
     * @return la velocidad aplicada tras acotar, o NAN si el indice o el valor no valen.
     */
    float apply(int idx, float speed);

    /** Lleva los 6 motores a 0 % (rueda libre). */
    void stopAll();

private:
    bool initialized_ = false;
};
