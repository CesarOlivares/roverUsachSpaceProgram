'use strict';

/*
 * Contrato con el firmware. Las acciones y campos tienen que coincidir con
 * src/transport/JsonProtocol.cpp y src/view/TelemetryView.cpp del sketch.
 */
const Action = Object.freeze({
    HEARTBEAT: 'HEARTBEAT',
    ARM: 'ARM',
    DISARM: 'DISARM',
    STOP: 'STOP',
    DRIVE: 'DRIVE',
    TEST_MOTOR: 'TEST_MOTOR',
    SERVO_SET: 'SERVO_SET',
    STEPPER_JOG: 'STEPPER_JOG',
});

const Protocol = {
    heartbeat: (seq) => ({ action: Action.HEARTBEAT, seq }),
    arm: () => ({ action: Action.ARM }),
    disarm: () => ({ action: Action.DISARM }),
    stop: () => ({ action: Action.STOP }),
    drive: (linear, angular, seq) => ({ action: Action.DRIVE, linear, angular, seq }),
    testMotor: (motor, speed, seq) => (seq === undefined
        ? { action: Action.TEST_MOTOR, motor, speed }
        : { action: Action.TEST_MOTOR, motor, speed, seq }),
    servoSet: (channel, angle) => ({ action: Action.SERVO_SET, channel, angle }),
    stepperJog: (steps, speed) => ({ action: Action.STEPPER_JOG, steps, speed }),

    encode(obj) { return JSON.stringify(obj); },

    /**
     * Decodifica una linea entrante. Devuelve la telemetria normalizada o null.
     *
     * En modo cable el log del firmware comparte puerto con la telemetria: todo
     * lo que no empiece por '{' es un log, no una trama.
     */
    decodeTelemetry(line) {
        if (!line || line[0] !== '{') return null;

        let data;
        try {
            data = JSON.parse(line);
        } catch (e) {
            return null;                        // trama partida, la siguiente llegara entera
        }
        if (data.type !== 'telemetry') return null;

        const list = (arr, n, fallback) => {
            const out = new Array(n).fill(fallback);
            if (Array.isArray(arr)) for (let i = 0; i < n; i++) out[i] = num(arr[i]);
            return out;
        };

        return {
            seq: typeof data.seq === 'number' ? data.seq : null,
            uptime: num(data.uptime),
            armed: typeof data.armed === 'boolean' ? data.armed : null,
            owner: typeof data.owner === 'string' ? data.owner : null,
            motors: Array.isArray(data.motors) ? list(data.motors, CONFIG.MOTOR_COUNT, 0) : null,
            servos: Array.isArray(data.servos) ? list(data.servos, CONFIG.SERVO_COUNT, 90) : null,
            stepperPos: num(data.stepper_pos),
            stepperMoving: Boolean(data.stepper_moving),
            rxOk: num(data.rx_ok),
            rxErr: num(data.rx_err),
            clients: num(data.clients),
            heap: num(data.heap),
        };
    },
};
