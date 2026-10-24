'use strict';

/** Mapa de pines con el estado en vivo de cada actuador. Solo lectura. */
class PinTableView {
    constructor(model) {
        this.model = model;

        this.motorBadges = [];
        for (let i = 0; i < CONFIG.MOTOR_COUNT; i++) {
            this.motorBadges[i] = Dom.byId(`pin-m${i + 1}-badge`);
        }
        this.servoBadges = [];
        for (let s = 0; s < CONFIG.SERVO_COUNT; s++) {
            this.servoBadges[s] = Dom.byId(`servo-dir-${s}-badge`);
        }
        this.stepper = Dom.byId('stepper-state-badge');
        this.i2c = Dom.byId('pin-i2c-badge');

        model.on('telemetry', () => this.render());
        model.on('link', () => this.renderBus());
        this.renderBus();
    }

    renderBus() {
        const live = this.model.connected && !this.model.link.stale;
        Dom.setPinBadge(this.i2c, live ? 'ACTIVO 50 Hz' : 'SIN ENLACE', live ? 'pin-i2c' : 'pin-idle');
    }

    render() {
        const t = this.model.telemetry;

        for (let i = 0; i < CONFIG.MOTOR_COUNT; i++) {
            const spd = t.motors[i];
            const active = Math.abs(spd) > 1;
            Dom.setPinBadge(this.motorBadges[i], active ? `${Math.round(spd)} %` : 'INACTIVO',
                            active ? 'pin-active' : 'pin-idle');
        }

        for (let s = 0; s < CONFIG.SERVO_COUNT; s++) {
            const ang = t.servos[s];
            Dom.setPinBadge(this.servoBadges[s], Format.steer(ang),
                            (ang < 80 || ang > 100) ? 'pin-active' : 'pin-i2c');
        }

        Dom.setPinBadge(this.stepper, t.stepperMoving ? 'EN MOVIMIENTO' : 'REPOSO',
                        t.stepperMoving ? 'pin-active' : 'pin-idle');
    }
}
