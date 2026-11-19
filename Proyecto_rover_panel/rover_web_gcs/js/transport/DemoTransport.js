'use strict';

/**
 * Simulador. Habla el mismo protocolo que la estacion base (incluido su estado
 * de radio) y usa la misma cinematica que el rover, asi que el resto de la
 * aplicacion no distingue si hay hardware al otro lado.
 *
 * Tambien imita el failsafe del firmware: sin ordenes durante FAILSAFE_MS corta
 * la traccion, y tras RETRY_COUNT ventanas desarma. simulateDrop(ms) corta la
 * "radio" ese tiempo para probar la reconexion desde el panel.
 */
class DemoTransport {
    constructor(onLine) {
        this.onLine = onLine;
        this.armed = false;
        this.owner = '';
        this.motors = new Array(CONFIG.MOTOR_COUNT).fill(0);
        this.servos = new Array(CONFIG.SERVO_COUNT).fill(CONFIG.STEER_CENTER);
        this.stepperPos = 0;
        this.uptime = 0;
        this.seq = 0;
        this.rxOk = 0;
        this.ticks = 0;
        this.timer = null;

        // Radio simulada
        this.dropUntil = 0;
        this.lastCmdAt = performance.now();
        this.txOk = 0;
        this.txFail = 0;
        this.window = [];             // ultimos 20 envios: true = con ACK
        this.arcs = [];
        this.retry = 0;
        this.reinit = 0;
        this.lastRetryAt = 0;
        this.started = performance.now();
    }

    async open() {
        this.timer = setInterval(() => this._tick(), 1000 / CONFIG.TX_HZ);
    }

    /** Corta la radio simulada durante ms milisegundos. */
    simulateDrop(ms) {
        this.dropUntil = performance.now() + ms;
    }

    get dropping() { return performance.now() < this.dropUntil; }

    _tick() {
        const now = performance.now();
        this.uptime += 1 / CONFIG.TX_HZ;
        this._failsafe(now);

        if (!this.dropping) {
            this.onLine(JSON.stringify({
                type: 'telemetry',
                seq: this.seq,
                uptime: Math.floor(this.uptime),
                armed: this.armed,
                owner: this.owner,
                motors: this.motors.map((v) => round1(v)),
                servos: this.servos.map((v) => Math.round(v)),
                stepper_pos: this.stepperPos,
                stepper_moving: false,
                rx_ok: this.rxOk,
                rx_err: 0,
                clients: 1,
                heap: 280000,
                radio_lq: this._lq(),
            }));
        }

        // Estado de la "estacion base" cada 500 ms, como la de verdad.
        if (++this.ticks % Math.round(CONFIG.TX_HZ / 2) === 0) {
            const arc = this.arcs.length ? this.arcs.reduce((a, b) => a + b, 0) / this.arcs.length : 0;
            this.onLine(JSON.stringify({
                type: 'base',
                rover: !this.dropping,
                lq: this._lq(),
                tx_ok: this.txOk,
                tx_fail: this.txFail,
                arc: Math.round(arc * 10),
                retry: this.retry,
                reinit: this.reinit,
                chip: true,
                up: Math.floor((now - this.started) / 1000),
            }));
        }
    }

    _lq() {
        if (this.window.length === 0) return 100;
        return Math.round(100 * this.window.filter(Boolean).length / this.window.length);
    }

    /** Lo que haria el firmware del rover y la base con la radio caida. */
    _failsafe(now) {
        const silence = now - this.lastCmdAt;
        if (silence > CONFIG.FAILSAFE_MS) {
            this.motors.fill(0);                 // traccion cortada, armado conservado

            const elapsed = silence - CONFIG.FAILSAFE_MS;
            const attempt = 1 + Math.floor(elapsed / CONFIG.RETRY_WINDOW_MS);
            if (attempt > this.retry && attempt <= CONFIG.RETRY_COUNT) {
                this.retry = attempt;            // la base reconfigura su radio
                this.reinit++;
            }
            if (attempt > CONFIG.RETRY_COUNT && (this.armed || this.owner)) {
                this.armed = false;
                this.owner = '';
                this.servos.fill(CONFIG.STEER_CENTER);
            }
        }
    }

    _stopAll() {
        this.motors.fill(0);
        this.servos.fill(CONFIG.STEER_CENTER);
    }

    _recordSend(ok) {
        this.window.push(ok);
        this.arcs.push(ok ? (Math.random() < 0.1 ? 1 : 0) : 5);
        if (this.window.length > 20) { this.window.shift(); this.arcs.shift(); }
        ok ? this.txOk++ : this.txFail++;
    }

    async send(text) {
        for (const line of text.split('\n')) {
            if (!line.trim()) continue;
            let cmd;
            try { cmd = JSON.parse(line); } catch (e) { continue; }

            // Radio caida: la orden no llega al rover.
            if (this.dropping) { this._recordSend(false); continue; }
            this._recordSend(true);
            this.lastCmdAt = performance.now();
            this.retry = 0;

            this.rxOk++;
            if (typeof cmd.seq === 'number') this.seq = cmd.seq;

            switch (cmd.action) {
                case Action.STOP:
                case Action.DISARM:
                    this.armed = false;
                    this.owner = '';
                    this._stopAll();
                    continue;
                case Action.HEARTBEAT:
                    continue;
                default:
                    break;
            }

            // Mismo arbitraje que el firmware: el primero que actua se queda el mando.
            if (this.owner === '') this.owner = cmd.client || '';
            else if (this.owner !== cmd.client) continue;

            switch (cmd.action) {
                case Action.ARM:
                    this.armed = true;
                    break;
                case Action.DRIVE: {
                    if (!this.armed) break;
                    this.motors.fill(Kinematics.tractionSpeed(cmd.linear));
                    Kinematics.steeringAngles(cmd.angular).forEach((a, ch) => { this.servos[ch] = a; });
                    break;
                }
                case Action.TEST_MOTOR:
                    // Todas las ruedas comparten puente H: probar una mueve las seis.
                    if (cmd.motor >= 0 && cmd.motor < CONFIG.MOTOR_COUNT) {
                        this.motors.fill(clamp(cmd.speed, -100, 100));
                    }
                    break;
                case Action.SERVO_SET:
                    if (cmd.channel >= 0 && cmd.channel < this.servos.length) {
                        this.servos[cmd.channel] = clamp(cmd.angle, 0, 180);
                    }
                    break;
                case Action.STEPPER_JOG:
                    this.stepperPos += cmd.steps;
                    break;
                default:
                    break;
            }
        }
    }

    async close() {
        clearInterval(this.timer);
        this.timer = null;
    }
}
