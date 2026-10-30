'use strict';

/** Mapa de pines con el estado en vivo de cada actuador. Solo lectura. */
class PinTableView {
    constructor(model) {
        this.model = model;

        this.traction = Dom.byId('pin-traction-badge');
        this.servoBadges = [];
        for (let s = 0; s < CONFIG.SERVO_COUNT; s++) {
            this.servoBadges[s] = Dom.byId(`servo-dir-${s}-badge`);
        }
        this.i2c = Dom.byId('pin-i2c-badge');
        this.nrf = Dom.byId('pin-nrf-badge');

        model.on('telemetry', () => this.render());
        model.on('link', () => this.renderBus());
        this.renderBus();
    }

    renderBus() {
        const live = this.model.connected && !this.model.link.stale;
        Dom.setPinBadge(this.i2c, live ? 'ACTIVO 50 Hz' : 'SIN ENLACE', live ? 'pin-i2c' : 'pin-idle');

        const radio = this.model.radio;
        if (!radio) {
            Dom.setPinBadge(this.nrf, this.model.connected ? 'SIN BASE' : 'SIN ENLACE', 'pin-idle');
        } else if (!radio.rover) {
            Dom.setPinBadge(this.nrf, 'SIN ROVER', 'pin-idle');
        } else {
            Dom.setPinBadge(this.nrf, `ENLACE ${radio.lq}%`, 'pin-active');
        }
    }

    render() {
        const t = this.model.telemetry;

        // Las 6 ruedas llevan la misma consigna: basta con la primera.
        const spd = t.motors[0];
        const active = Math.abs(spd) > 1;
        Dom.setPinBadge(this.traction, active ? `${Math.round(spd)} %` : 'INACTIVO',
                        active ? 'pin-active' : 'pin-idle');

        for (let s = 0; s < CONFIG.SERVO_COUNT; s++) {
            const ang = t.servos[s];
            Dom.setPinBadge(this.servoBadges[s], Format.steer(ang),
                            (ang < 80 || ang > 100) ? 'pin-active' : 'pin-i2c');
        }
    }
}
