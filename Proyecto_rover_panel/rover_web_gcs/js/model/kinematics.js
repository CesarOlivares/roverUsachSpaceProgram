'use strict';

/*
 * Misma formula que src/model/Kinematics.cpp del firmware. La usa el simulador
 * para que la simulacion se comporte igual que el rover real.
 */
const Kinematics = {
    /** Mezcla diferencial con escalado proporcional al saturar. */
    mixDifferential(linear, angular) {
        let left = linear + angular;
        let right = linear - angular;
        const peak = Math.max(Math.abs(left), Math.abs(right));
        if (peak > 100) {
            left = (left / peak) * 100;
            right = (right / peak) * 100;
        }
        return { left, right };
    },

    steeringAngle(angular) {
        return clamp(CONFIG.STEER_CENTER + (angular / 100) * CONFIG.STEER_THROW,
                     CONFIG.STEER_CENTER - CONFIG.STEER_THROW,
                     CONFIG.STEER_CENTER + CONFIG.STEER_THROW);
    },
};
