'use strict';

/*
 * Misma formula que src/model/Kinematics.cpp del firmware. La usa el simulador
 * para que la simulacion se comporte igual que el rover real.
 *
 * Un unico puente H (BTS7960) mueve las 6 ruedas: la traccion sale de "linear"
 * y el giro solo de los servos de direccion.
 */
const Kinematics = {
    tractionSpeed(linear) {
        return clamp(linear, -100, 100);
    },

    steeringAngle(angular) {
        return clamp(CONFIG.STEER_CENTER + (angular / 100) * CONFIG.STEER_THROW,
                     CONFIG.STEER_CENTER - CONFIG.STEER_THROW,
                     CONFIG.STEER_CENTER + CONFIG.STEER_THROW);
    },
};
