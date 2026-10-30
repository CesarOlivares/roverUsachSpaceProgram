#pragma once

#include "../config/RoverConfig.h"

/*
 * Cinematica del rover. Funciones puras: sin hardware ni RTOS, para poder
 * probarlas fuera de la placa y compartir la misma formula con el simulador
 * del panel (rover_web_gcs/js/model/kinematics.js).
 *
 * Con un unico puente H para las 6 ruedas no hay traccion diferencial: el
 * avance sale de "linear" y el giro solo de los servos de direccion (4WS).
 */

namespace kinematics {

/** Consigna de traccion (-100..100) para un DRIVE. */
float tractionSpeed(float linear, float angular);

/**
 * Angulos de los 4 servos de esquina (FL, FR, RL, RR) para una consigna de
 * giro (-100..100). Delanteros y traseros giran en sentido opuesto.
 */
void steeringAngles(float angular, float out[cfg::kSteeringServosInDrive]);

}  // namespace kinematics
