'use strict';

/**
 * Conduccion: armado, entradas (teclado y mando) y el lazo de transmision,
 * que es el unico sitio que emite tramas periodicas.
 */
class DriveController {
    constructor(model, link) {
        this.model = model;
        this.link = link;
        this.timer = null;

        // Si el rover no confirmo un desarmado, se insiste: nunca se da por
        // armado un rover al que se le pidio parar.
        model.on('unconfirmed', ({ target }) => {
            if (target) {
                model.emit('notice', 'El rover no confirmó el armado. Sigue desarmado.');
            } else if (this.link.connected) {
                this.disarm();
            }
        });
    }

    /* --- Lazo TX --- */

    start() {
        this.stop();
        this.timer = setInterval(() => this.tick(), 1000 / CONFIG.TX_HZ);
    }

    stop() {
        if (this.timer) clearInterval(this.timer);
        this.timer = null;
    }

    tick() {
        if (!this.link.connected) return;

        const m = this.model;
        const now = performance.now();
        m.refreshStale(now);

        const seq = m.nextSeq(now);

        /*
         * Exactamente una trama por ciclo, elegida por prioridad, para que nunca
         * salgan dos ordenes contradictorias en el mismo milisegundo.
         */
        if (!m.hasControl()) {
            // Observador: solo late. El firmware rechaza sus ordenes y sus
            // latidos ya no sostienen el failsafe del operador al mando.
            this.link.sendFrame(Protocol.heartbeat(seq));
        } else if (m.manual) {
            this.link.sendFrame(Protocol.testMotor(m.manual.motor, m.manual.speed, seq));
        } else if (m.armed) {
            this.computeDrive();
            this.link.sendFrame(Protocol.drive(round1(m.drive.linear), round1(m.drive.angular), seq));
        } else {
            this.link.sendFrame(Protocol.heartbeat(seq));
        }
    }

    /* --- Armado --- */

    arm() {
        this.model.requestArm(performance.now());
        this.link.sendEvent(Protocol.arm());
    }

    disarm() {
        this.model.requestDisarm(performance.now());
        this.link.sendEvent(Protocol.disarm());
    }

    toggleArm() {
        const m = this.model;
        (m.armed || m.armPending?.target) ? this.disarm() : this.arm();
    }

    /* --- Entradas --- */

    bindInputs() {
        window.addEventListener('keydown', (e) => this._onKey(e, true));
        window.addEventListener('keyup', (e) => this._onKey(e, false));

        // Al perder el foco se sueltan las teclas: si no, una tecla "pegada"
        // dejaria al rover avanzando solo.
        window.addEventListener('blur', () => this.model.releaseKeys());

        window.addEventListener('gamepadconnected', () => this.model.setGamepad(true));
        window.addEventListener('gamepaddisconnected', () => this.model.setGamepad(false));
    }

    _onKey(e, down) {
        if (Dom.isTypingTarget(e.target)) return;
        if (!['KeyW', 'KeyA', 'KeyS', 'KeyD'].includes(e.code)) return;
        if (e.repeat) return;
        this.model.setKey(e.code[3].toLowerCase(), down);
    }

    /** Calcula linear/angular. El mando tiene prioridad sobre el teclado. */
    computeDrive() {
        let left = 0;
        let right = 0;

        const { w, a, s, d } = this.model.keys;
        if (w !== s) {
            const fwd = w ? 100 : -100;
            // Con giro simultaneo se frena la rueda interior en vez de invertirla.
            if (a !== d) {
                const inner = 0;
                if (a) { left = w ? inner : fwd; right = w ? fwd : inner; }
                else { left = w ? fwd : inner; right = w ? inner : fwd; }
            } else {
                left = fwd; right = fwd;
            }
        } else if (a !== d) {
            // Solo giro: rotacion sobre el propio eje.
            left = a ? -100 : 100;
            right = a ? 100 : -100;
        }

        const gp = navigator.getGamepads?.()[0];
        if (gp) {
            const rawLin = -gp.axes[1];
            const rawAng = gp.axes[0];
            if (Math.abs(rawLin) > 0.12 || Math.abs(rawAng) > 0.12) {
                left = clamp((rawLin + rawAng) * 100, -100, 100);
                right = clamp((rawLin - rawAng) * 100, -100, 100);
            }
        }

        this.model.drive.linear = (left + right) / 2;
        this.model.drive.angular = (left - right) / 2;
    }
}
