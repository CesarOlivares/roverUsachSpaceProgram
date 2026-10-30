'use strict';

/**
 * Simulador. Habla el mismo protocolo que la estacion base (incluido su estado
 * de radio) y usa la misma cinematica que el rover, asi que el resto de la
 * aplicacion no distingue si hay hardware al otro lado.
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
    }

    async open() {
        this.timer = setInterval(() => this._tick(), 1000 / CONFIG.TX_HZ);
    }

    _tick() {
        this.uptime += 1 / CONFIG.TX_HZ;

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
            radio_lq: 100,
        }));

        // Estado de la "estacion base" cada 500 ms, como la de verdad.
        if (++this.ticks % Math.round(CONFIG.TX_HZ / 2) === 0) {
            this.onLine(JSON.stringify({
                type: 'base', rover: true, lq: 100, tx_ok: this.rxOk, tx_fail: 0,
            }));
        }
    }

    _stopAll() {
        this.motors.fill(0);
        this.servos.fill(CONFIG.STEER_CENTER);
    }

    async send(text) {
        for (const line of text.split('\n')) {
            if (!line.trim()) continue;
            let cmd;
            try { cmd = JSON.parse(line); } catch (e) { continue; }

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
