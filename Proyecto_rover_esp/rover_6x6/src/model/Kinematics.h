#pragma once

/*
 * Cinematica del rover. Funciones puras: sin hardware ni RTOS, para poder
 * probarlas fuera de la placa y compartir la misma formula con el simulador
 * del panel (rover_web_gcs/js/model/kinematics.js).
 */

namespace kinematics {

struct SideSpeeds {
    float left;    // M1, M3, M5
    float right;   // M2, M4, M6
};

/**
 * Mezcla diferencial. linear y angular en -100..100.
 * Si un lado satura, escala los dos en proporcion para conservar el radio de giro.
 */
SideSpeeds mixDifferential(float linear, float angular);

/** Angulo comun de los servos de direccion para una consigna de giro (-100..100). */
float steeringAngle(float angular);

}  // namespace kinematics
