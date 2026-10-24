'use strict';

/**
 * Estado unico del panel. No conoce ni el DOM ni el transporte: los
 * controladores lo modifican y las vistas se suscriben a sus eventos.
 *
 * Eventos:
 *   link       cambio de modo, de frescura o de latencia del enlace
 *   arm        cambio del armado confirmado o de una orden pendiente
 *   unconfirmed  el rover no confirmo un armado/desarmado a tiempo ({ target })
 *   control    cambio de quien tiene el mando
 *   manual     empieza o termina una prueba manual de motor
 *   inputs     teclado / mando activos
 *   telemetry  llego telemetria nueva
 *   wire       { dir: 'tx'|'rx', text } ultima trama en cada sentido
 *   notice     mensaje para el operador
 */
class RoverModel extends Emitter {
    constructor() {
        super();

        this.link = { mode: 'none', stale: true, latency: null };

        /*
         * El armado lo decide el firmware. `armed` es solo lo que la telemetria
         * confirmo; `armPending` es una orden enviada y aun sin confirmar. Antes
         * el panel se daba por armado al pulsar, y una telemetria generada justo
         * antes del ARM lo desarmaba y avisaba de un failsafe que no habia pasado.
         */
        this.armed = false;
        this.armPending = null;          // { target: bool, at: ms } | null

        // '' = mando libre. Distinto de CLIENT_ID = otro operador esta al mando.
        this.owner = '';

        // Prueba manual de un motor. null = sin prueba activa.
        // Mientras no sea null, el lazo TX manda TEST_MOTOR en vez de DRIVE.
        this.manual = null;

        this.keys = { w: false, a: false, s: false, d: false };
        this.gamepad = false;
        this.drive = { linear: 0, angular: 0 };

        this.telemetry = {
            uptime: 0,
            motors: new Array(CONFIG.MOTOR_COUNT).fill(0),
            // Angulos reales de consigna, publicados por el firmware. No se calculan
            // aqui: con varios clientes conectados cada uno veria algo distinto.
            servos: new Array(CONFIG.SERVO_COUNT).fill(90),
            stepperPos: 0, stepperMoving: false,
            rxOk: 0, rxErr: 0,
            clients: 0, heap: 0,
        };

        this.seq = 0;
        this.pendingPings = new Map();   // seq -> instante de envio, para medir el RTT real
        this.lastRxAt = 0;
    }

    /* --- Consultas --- */

    get connected() { return this.link.mode !== 'none'; }

    /** true si este panel puede actuar: o tiene el mando, o esta libre. */
    hasControl() { return this.owner === '' || this.owner === CLIENT_ID; }

    /* --- Enlace --- */

    beginSession(mode) {
        this.link = { mode, stale: true, latency: null };
        this.armed = false;
        this.armPending = null;
        this.manual = null;
        this.lastRxAt = 0;
        this.pendingPings.clear();
        this.emit('link');
        this.emit('arm');
    }

    endSession() {
        this.link = { mode: 'none', stale: true, latency: null };
        this.armed = false;
        this.armPending = null;
        this.manual = null;
        this.pendingPings.clear();
        this.emit('link');
        this.emit('arm');
        this.emit('manual');
    }

    /** Recalcula si el enlace esta caido: sin telemetria reciente no se pinta latencia vieja. */
    refreshStale(now) {
        const stale = this.lastRxAt === 0 || (now - this.lastRxAt) > CONFIG.STALE_MS;
        if (stale === this.link.stale) return;
        this.link.stale = stale;
        if (stale) this.link.latency = null;
        this.emit('link');
    }

    /** Siguiente numero de secuencia; se guarda el instante para medir el RTT. */
    nextSeq(now) {
        const seq = ++this.seq;
        this.pendingPings.set(seq, now);
        if (this.pendingPings.size > 64) this.pendingPings.clear();
        return seq;
    }

    /* --- Armado --- */

    requestArm(now) {
        this.armPending = { target: true, at: now };
        this.emit('arm');
    }

    /** El desarmado se aplica ya en local: el panel deja de mandar DRIVE en el acto. */
    requestDisarm(now) {
        this.armed = false;
        this.armPending = { target: false, at: now };
        this.setManual(null);
        this.emit('arm');
    }

    /* --- Entradas y pruebas --- */

    setManual(manual) {
        this.manual = manual;
        this.emit('manual');
    }

    setKey(key, down) {
        this.keys[key] = down;
        this.emit('inputs');
    }

    releaseKeys() {
        this.keys = { w: false, a: false, s: false, d: false };
        this.emit('inputs');
    }

    setGamepad(connected) {
        this.gamepad = connected;
        this.emit('inputs');
    }

    get keyboardActive() { return Object.values(this.keys).some(Boolean); }

    /** Ultima trama en cada sentido, para el monitor de enlace. */
    noteWire(dir, text) {
        this.emit('wire', { dir, text });
    }

    /* --- Telemetria entrante --- */

    applyTelemetry(t, now) {
        this.lastRxAt = now;

        const tel = this.telemetry;
        tel.uptime = t.uptime;
        tel.stepperPos = t.stepperPos;
        tel.stepperMoving = t.stepperMoving;
        tel.rxOk = t.rxOk;
        tel.rxErr = t.rxErr;
        tel.clients = t.clients;
        tel.heap = t.heap;
        if (t.motors) tel.motors = t.motors;
        if (t.servos) tel.servos = t.servos;

        if (t.armed !== null) this._reconcileArm(t.armed, now);

        // Arbitraje del mando: si otro panel lo tiene, este pasa a observador.
        if (t.owner !== null && t.owner !== this.owner) {
            const hadControl = this.hasControl();
            this.owner = t.owner;
            this.emit('control');
            if (hadControl && !this.hasControl()) {
                this.setManual(null);
                this.emit('notice', 'Otro operador tomó el mando. Este panel queda en observación.');
            } else if (!hadControl && this.hasControl()) {
                this.emit('notice', 'Mando disponible para este panel.');
            }
        }

        // RTT real: el firmware devuelve el ultimo seq recibido.
        if (t.seq !== null) {
            const sentAt = this.pendingPings.get(t.seq);
            if (sentAt !== undefined) {
                this.link.latency = Math.round(now - sentAt);
                this.pendingPings.clear();      // los anteriores ya no interesan
            }
        }

        this.emit('telemetry');
    }

    _reconcileArm(remote, now) {
        const pending = this.armPending;
        if (pending) {
            if (remote === pending.target) {
                this.armPending = null;
                this.armed = remote;
                this.emit('arm');
                return;
            }
            // Telemetria generada antes de que el rover procesara la orden.
            if (now - pending.at < CONFIG.ARM_CONFIRM_MS) return;

            this.armPending = null;
            this.emit('arm');
            this.emit('unconfirmed', { target: pending.target });
            if (!pending.target) return;        // el controlador reintenta el desarmado
        }

        if (remote !== this.armed) {
            const wasArmed = this.armed;
            this.armed = remote;
            this.emit('arm');
            /*
             * El firmware manda. Si su failsafe desarmo el rover por perdida de
             * enlace, el panel tiene que enterarse en vez de seguir mostrando ARMADO.
             */
            if (wasArmed && !remote) {
                this.setManual(null);
                this.emit('notice', 'El rover se desarmó (failsafe del firmware u orden de parada).');
            }
        }
    }
}
