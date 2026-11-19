'use strict';

/**
 * Servo 5 (canal 4): gesto de asentir. Mueve el servo adelante y atras unas
 * cuantas veces y lo deja en el centro.
 *
 * Usa SERVO_SET, que solo pide tener el mando (no hace falta armar): el canal 4
 * no participa en la direccion, asi que no choca con DRIVE.
 *
 * Cada gesto lleva su token: una parada de emergencia o una desconexion lo
 * invalidan y el bucle deja de mandar posiciones. Si no, el servo seguiria
 * moviendose despues de pulsar STOP.
 */
class MastController {
    constructor(model, diagnostics) {
        this.model = model;
        this.diagnostics = diagnostics;
        this.token = 0;
        this.busy = false;

        model.on('estop', () => this.cancel());
        model.on('link', () => { if (!model.connected) this.cancel(); });
    }

    async nod() {
        if (this.busy || !this.model.connected || !this.model.hasControl()) return;

        const token = ++this.token;
        this._setBusy(true);

        const { CHANNEL, CENTER, AMPLITUDE, CYCLES, STEP_MS } = CONFIG.NOD;
        const sequence = [];
        for (let i = 0; i < CYCLES; i++) sequence.push(CENTER + AMPLITUDE, CENTER - AMPLITUDE);
        sequence.push(CENTER);

        try {
            for (const angle of sequence) {
                if (this.token !== token) return;
                this.diagnostics.setServo(CHANNEL, clamp(angle, 0, 180));
                await sleep(STEP_MS);
            }
        } finally {
            if (this.token === token) this._setBusy(false);
        }
    }

    /** Corta el gesto en curso. El firmware ya recentra el servo al parar. */
    cancel() {
        if (!this.busy) return;
        this.token++;
        this._setBusy(false);
    }

    _setBusy(busy) {
        this.busy = busy;
        this.model.emit('nod', busy);
    }
}
