#pragma once

/*
 * Cinematica del rover. Funciones puras: sin hardware ni RTOS, para poder
 * probarlas fuera de la placa y compartir la misma formula con el simulador
 * del panel (rover_web_gcs/js/model/kinematics.js).
 *
 * Con un unico puente H para las 6 ruedas no hay traccion diferencial: el
 * avance sale de "linear" y el giro solo de los servos de direccion. Por eso
 * el rover ya no puede girar sobre su propio eje.
 */

namespace kinematics {

/** Consigna de traccion (-100..100) para un DRIVE. */
float tractionSpeed(float linear, float angular);

/** Angulo comun de los servos de direccion para una consigna de giro (-100..100). */
float steeringAngle(float angular);

}  // namespace kinematics
