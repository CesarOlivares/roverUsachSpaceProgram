'use strict';

/**
 * Pestana de diagnostico: deslizadores de motores y servos, paso a paso.
 * Intenciones: 'motor' { motor, speed }, 'stop-all', 'servo' { channel, angle },
 * 'jog' { steps }.
 */
class DiagnosticsView extends Emitter {
    constructor(model) {
        super();
        this.model = model;

        this.motorItems = document.querySelectorAll('.motor-control-item');
        this.motorItems.forEach((item) => {
            const input = item.querySelector('input[type="range"]');
            const display = item.querySelector('.val-display');
            input?.addEventListener('input', () => {
                Dom.setText(display, `${input.value}%`);
                this.emit('motor', { motor: Number(item.dataset.motor), speed: Number(input.value) });
            });
        });

        // El angulo mostrado NO se fija aqui: llega de vuelta en la telemetria,
        // que es lo que el rover tiene puesto de verdad.
        document.querySelectorAll('[data-servo]').forEach((input) => {
            input.addEventListener('input', () => this.emit('servo', {
                channel: Number(input.dataset.servo),
                angle: Number(input.value),
            }));
        });

        document.querySelectorAll('[data-jog]').forEach((btn) => {
            btn.addEventListener('click', () => this.emit('jog', { steps: Number(btn.dataset.jog) }));
        });

        Dom.byId('btn-stop-all-motors')?.addEventListener('click', () => this.emit('stop-all'));

        this.servoLabels = [];
        for (let s = 0; s < CONFIG.SERVO_COUNT; s++) this.servoLabels[s] = Dom.byId(`servo-${s}-val`);
        this.stepperPos = Dom.byId('stepper-pos-val');

        model.on('telemetry', () => this.render());
    }

    resetMotorSliders() {
        this.motorItems.forEach((item) => {
            const input = item.querySelector('input[type="range"]');
            if (input) input.value = 0;
            Dom.setText(item.querySelector('.val-display'), '0%');
        });
    }

    render() {
        const t = this.model.telemetry;
        for (let s = 0; s < CONFIG.SERVO_COUNT; s++) Dom.setText(this.servoLabels[s], Format.steer(t.servos[s]));
        Dom.setText(this.stepperPos, String(t.stepperPos));
    }
}
