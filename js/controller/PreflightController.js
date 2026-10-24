'use strict';

/**
 * Asistente de pre-vuelo: prueba uno a uno los servos de direccion y los
 * motores, con confirmacion del operario en cada paso.
 *
 * Cada ejecucion lleva su token y se cancela sola: con setTimeout sueltos, si
 * el operario pulsaba "Si" rapido, los temporizadores del paso anterior
 * seguian vivos y pisaban los comandos del paso nuevo.
 *
 * Al terminar NO arma el rover: el armado es siempre una decision explicita
 * del operario, justo cuando va a conducir.
 */
class PreflightController {
    constructor(model, link, view, drive, diagnostics, safety) {
        this.model = model;
        this.link = link;
        this.view = view;
        this.drive = drive;
        this.diagnostics = diagnostics;
        this.safety = safety;

        this.token = 0;
        this.active = false;
        this.resolveAnswer = null;

        view.on('answer', (confirmed) => this.answer(confirmed));
    }

    get totalSteps() { return 1 + CONFIG.STEERING_SERVOS + CONFIG.MOTOR_COUNT; }

    async start() {
        const token = ++this.token;
        this.active = true;

        // El pre-vuelo mueve servos y motores uno a uno: nunca con el rover armado.
        if (this.model.armed || this.model.armPending?.target) this.drive.disarm();

        this.view.show(true);
        this.view.setBusy(false);

        const completed = await this._run(token);

        // Si el token cambio, otra ejecucion o una cancelacion tomo el relevo y
        // ya se encargo de cerrar el modal y de parar el hardware.
        if (this.token !== token) return;

        this.active = false;
        this.model.setManual(null);
        this.view.setBusy(false);
        this.view.show(false);

        if (completed) {
            this.model.emit('notice', 'Pre-vuelo completado. Arma el rover cuando vayas a conducir.');
        }
    }

    async _run(token) {
        const alive = () => this.token === token;
        const m = this.model;

        this._step(1, 'Comprobación del enlace',
            `Enlace: <strong>${m.radio ? `RADIO nRF24 (${m.radio.lq}%)` : m.link.mode.toUpperCase()}</strong> · ` +
            `latencia <strong>${m.link.latency ?? '--'} ms</strong> · ` +
            `tramas con error: <strong>${m.telemetry.rxErr}</strong><br><br>` +
            `Se probarán ${CONFIG.STEERING_SERVOS} servos y ${CONFIG.MOTOR_COUNT} motores ` +
            `uno a uno. Aparta las manos del chasis antes de continuar.`);
        if (!await this._await(token)) return false;

        // Solo los servos de direccion: el 5.o esta en suspension.
        for (let ch = 0; ch < CONFIG.STEERING_SERVOS; ch++) {
            if (!alive()) return false;
            this._step(2 + ch, `Servo ${ch + 1} (canal ${ch})`,
                'Barrido izquierda 45° → derecha 135° → centro 90°. ¿Se movió en ambos sentidos?');

            this.view.setBusy(true);
            for (const angle of [45, 135, 90]) {
                if (!alive()) return false;
                this.diagnostics.setServo(ch, angle);
                await sleep(600);
            }
            this.view.setBusy(false);
            if (!await this._await(token)) return false;
        }

        for (let mot = 0; mot < CONFIG.MOTOR_COUNT; mot++) {
            if (!alive()) return false;
            this._step(2 + CONFIG.STEERING_SERVOS + mot, `Motor ${MOTOR_NAMES[mot]}`,
                `Impulso de avance y retroceso al ${CONFIG.WIZARD_SPEED} %. ¿Giró en ambos sentidos?`);

            this.view.setBusy(true);
            for (const speed of [CONFIG.WIZARD_SPEED, -CONFIG.WIZARD_SPEED, 0]) {
                if (!alive()) { m.setManual(null); return false; }
                if (speed === 0) {
                    m.setManual(null);
                    this.link.sendEvent(Protocol.testMotor(mot, 0));
                } else {
                    m.setManual({ motor: mot, speed });
                }
                await sleep(700);
            }
            this.view.setBusy(false);
            if (!await this._await(token)) return false;
        }

        return true;
    }

    _step(step, title, html) {
        this.view.renderStep(step, this.totalSteps, title, html);
    }

    /** Espera la respuesta del operario. false si cancela o si el token caduca. */
    _await(token) {
        return new Promise((resolve) => {
            this.resolveAnswer = (answer) => {
                this.resolveAnswer = null;
                resolve(this.token === token && answer);
            };
        });
    }

    answer(confirmed) {
        if (confirmed) {
            this.resolveAnswer?.(true);         // se ignora si hay un actuador en marcha
            return;
        }
        this.cancel('Pre-vuelo cancelado. El rover sigue desarmado.');
    }

    /**
     * Cancelacion inmediata. No pasa por _await a proposito: durante un barrido
     * de servo o un impulso de motor no hay nadie esperando respuesta, y el
     * boton de cancelar tiene que funcionar justo en ese momento.
     */
    cancel(notice) {
        if (!this.active) return;
        this._invalidate();
        this.safety.emergencyStop();
        if (notice) this.model.emit('notice', notice);
    }

    /** Cancelacion silenciosa al desconectar. */
    abort() {
        if (!this.active) return;
        this._invalidate();
        this.model.setManual(null);
    }

    _invalidate() {
        this.token++;                            // invalida la ejecucion en curso
        this.resolveAnswer?.(false);
        this.resolveAnswer = null;
        this.active = false;
        this.view.setBusy(false);
        this.view.show(false);
    }
}
