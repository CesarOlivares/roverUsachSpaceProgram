'use strict';

/**
 * Servo 5: boton de asentir y angulo actual.
 * Hay un boton en Operacion y otro en Diagnostico; los dos llevan data-nod.
 * Intenciones: 'nod'.
 */
class MastView extends Emitter {
    constructor(model) {
        super();
        this.model = model;
        this.buttons = document.querySelectorAll('[data-nod]');
        this.angle = Dom.byId('mast-angle-val');
        this.tilt = Dom.byId('mast-tilt');

        this.buttons.forEach((btn) => btn.addEventListener('click', () => this.emit('nod')));

        model.on('nod', (busy) => this.renderBusy(busy));
        model.on('telemetry', () => this.render());
    }

    renderBusy(busy) {
        this.buttons.forEach((btn) => {
            btn.classList.toggle('busy', busy);
            btn.querySelector('.btn-label').textContent = busy ? 'ASINTIENDO…' : 'ASENTIR';
        });
    }

    render() {
        const a = this.model.telemetry.servos[CONFIG.NOD.CHANNEL];
        Dom.setText(this.angle, `${Math.round(a)}°`);
        // Cabeceo dibujado de lado: 0 = centro.
        this.tilt?.style.setProperty('--tilt', `${a - CONFIG.NOD.CENTER}deg`);
    }
}
