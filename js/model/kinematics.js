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

    /**
     * Direccion 4WS: [FL, FR, RL, RR]. Delanteros 90+delta, traseros 90-delta.
     * (El firmware ademas aplica el trim e inversion de cada servo.)
     */
    steeringAngles(angular) {
        const delta = (clamp(angular, -100, 100) / 100) * CONFIG.STEER_THROW;
        return [0, 1, 2, 3].map((ch) => CONFIG.STEER_CENTER + (ch < 2 ? delta : -delta));
    },
};
