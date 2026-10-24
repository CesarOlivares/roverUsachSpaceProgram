'use strict';

/**
 * Esquema superior 6x6 y popover de prueba rapida de un motor.
 * Intenciones: 'hold' { motor, sign } al mantener pulsado, 'release' al soltar
 * o al cerrar/cambiar el popover.
 */
class SchematicView extends Emitter {
    constructor(model) {
        super();
        this.model = model;
        this.popoverMotor = null;

        this.container = document.querySelector('.rover-view');
        this.popover = Dom.byId('motor-test-popover');
        this.popoverTitle = Dom.byId('popover-title');

        this.wheels = [];
        this.wheelVals = [];
        for (let i = 0; i < CONFIG.MOTOR_COUNT; i++) {
            const wheel = Dom.byId(`m${i + 1}`);
            this.wheels[i] = wheel;
            this.wheelVals[i] = wheel?.querySelector('.wheel-val');
            wheel?.addEventListener('click', (e) => {
                e.stopPropagation();
                this.openPopover(i);
            });
        }

        document.addEventListener('click', () => this.closePopover());
        this.popover?.addEventListener('click', (e) => e.stopPropagation());
        Dom.byId('btn-popover-close')?.addEventListener('click', () => this.closePopover());

        this._bindHold('btn-popover-forward', 1);
        this._bindHold('btn-popover-backward', -1);

        model.on('telemetry', () => this.render());
    }

    _bindHold(id, sign) {
        const btn = Dom.byId(id);
        if (!btn) return;

        const start = (e) => {
            e.preventDefault();
            if (this.popoverMotor === null) return;
            this.emit('hold', { motor: this.popoverMotor, sign });
        };
        const stop = (e) => {
            e.preventDefault();
            this.emit('release');
        };

        btn.addEventListener('mousedown', start);
        btn.addEventListener('touchstart', start, { passive: false });
        btn.addEventListener('mouseup', stop);
        btn.addEventListener('mouseleave', stop);
        btn.addEventListener('touchend', stop);
        btn.addEventListener('touchcancel', stop);
    }

    openPopover(index) {
        this.emit('release');
        this.popoverMotor = index;

        const wheel = this.wheels[index];
        if (!this.popover || !this.container || !wheel) return;

        Dom.setText(this.popoverTitle, MOTOR_NAMES[index]);

        const wheelRect = wheel.getBoundingClientRect();
        const boxRect = this.container.getBoundingClientRect();
        const top = clamp(wheelRect.top - boxRect.top + wheelRect.height / 2 - 60,
                          10, boxRect.height - 120);
        const left = clamp(wheelRect.left - boxRect.left + wheelRect.width / 2 - 85,
                           10, boxRect.width - 180);

        this.popover.style.top = `${top}px`;
        this.popover.style.left = `${left}px`;
        this.popover.classList.remove('hidden');
    }

    closePopover() {
        this.emit('release');
        this.popoverMotor = null;
        this.popover?.classList.add('hidden');
    }

    render() {
        const motors = this.model.telemetry.motors;
        for (let i = 0; i < CONFIG.MOTOR_COUNT; i++) {
            const spd = motors[i];
            const active = Math.abs(spd) > 1;

            Dom.setText(this.wheelVals[i], `${Math.round(spd)}%`);

            const wheel = this.wheels[i];
            if (wheel) {
                wheel.classList.toggle('active', active);
                wheel.classList.toggle('reverse', spd < -1);
                wheel.style.setProperty('--wheel-power', `${Math.abs(spd)}%`);
            }
        }
    }
}
