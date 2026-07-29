/**
 * Rover 6x6 - Estacion de control en tierra
 *
 * Capas:
 *   Transport  transporte concreto (serie USB / WebSocket / simulador)
 *   Link       cola de salida unica + parseo de telemetria entrante
 *   State      unica fuente de verdad
 *   Input      teclado y mando -> vector de conduccion
 *   Tx         un solo lazo a 20 Hz que decide que trama sale
 *   View       pintado del DOM
 *   Wizard     secuencia de pre-vuelo cancelable
 *   App        cableado y manejadores que usa el HTML
 */

'use strict';

const CONFIG = {
    TX_HZ: 20,                  // debe ir muy por encima del failsafe del firmware (400 ms)
    STALE_MS: 800,              // sin telemetria durante este tiempo el enlace se marca caido
    BAUD_RATE: 115200,
    MAX_QUEUED_EVENTS: 32,
    TEST_SPEED: 50,             // % de las pruebas rapidas desde el esquema del rover
    WIZARD_SPEED: 40,           // % de las pruebas del asistente de pre-vuelo
    STEERING_CHANNELS: 5,
    MOTOR_COUNT: 6,
};

const MOTOR_NAMES = [
    'M1 (Delantero Izq)', 'M2 (Delantero Der)',
    'M3 (Central Izq)', 'M4 (Central Der)',
    'M5 (Trasero Izq)', 'M6 (Trasero Der)',
];

/* ========================================================================= */
/* Estado                                                                    */
/* ========================================================================= */

const State = {
    link: { mode: 'none', connected: false, stale: true, latency: null },
    armed: false,
    keys: { w: false, a: false, s: false, d: false },
    drive: { linear: 0, angular: 0 },

    // Prueba manual de un motor. null = sin prueba activa.
    // Mientras no sea null, el lazo TX manda TEST_MOTOR en vez de DRIVE.
    manual: null,

    servoAngles: new Array(CONFIG.STEERING_CHANNELS).fill(90),

    telemetry: {
        batteryV: 0, rawMv: 0, uptime: 0,
        motors: new Array(CONFIG.MOTOR_COUNT).fill(0),
        stepperPos: 0, stepperMoving: false,
        rxOk: 0, rxErr: 0,
    },

    seq: 0,
    pendingPings: new Map(),    // seq -> instante de envio, para medir el RTT real
};

/* ========================================================================= */
/* Transportes                                                               */
/* ========================================================================= */

/** Cable USB, API Web Serial. Solo Chrome/Edge sobre http(s) o file://. */
class SerialTransport {
    constructor(onLine, onClose) {
        this.onLine = onLine;
        this.onClose = onClose;
        this.port = null;
        this.writer = null;
        this.reader = null;
        this.closing = false;
    }

    static get available() { return 'serial' in navigator; }

    async open() {
        if (!SerialTransport.available) {
            throw new Error('Este navegador no soporta Web Serial. Usa Chrome o Edge.');
        }
        this.port = await navigator.serial.requestPort();
        await this.port.open({ baudRate: CONFIG.BAUD_RATE });
        this.writer = this.port.writable.getWriter();
        this._readLoop();
    }

    async _readLoop() {
        const decoder = new TextDecoderStream();
        this.port.readable.pipeTo(decoder.writable).catch(() => { /* cierre normal */ });
        this.reader = decoder.readable.getReader();

        let buffer = '';
        try {
            while (true) {
                const { value, done } = await this.reader.read();
                if (done) break;
                if (!value) continue;

                buffer += value;
                const lines = buffer.split('\n');
                buffer = lines.pop();            // la ultima queda incompleta

                for (const line of lines) this.onLine(line);

                // Sin fin de linea a la vista es que llega ruido: se descarta
                // el buffer para no crecer sin limite.
                if (buffer.length > 4096) buffer = '';
            }
        } catch (err) {
            if (!this.closing) console.error('Fallo leyendo el puerto serie:', err);
        } finally {
            this.onClose();
        }
    }

    async send(text) {
        if (!this.writer) throw new Error('Puerto serie cerrado');
        await this.writer.write(new TextEncoder().encode(text));
    }

    async close() {
        this.closing = true;
        try { await this.reader?.cancel(); } catch (e) { /* ya cerrado */ }
        try { this.writer?.releaseLock(); } catch (e) { /* ya liberado */ }
        try { await this.port?.close(); } catch (e) { /* ya cerrado */ }
        this.writer = null;
    }
}

/** Wi-Fi, WebSocket contra el SoftAP del ESP32-S3. */
class WsTransport {
    constructor(onLine, onClose) {
        this.onLine = onLine;
        this.onClose = onClose;
        this.socket = null;
    }

    open(ip, port) {
        return new Promise((resolve, reject) => {
            const url = `ws://${ip}:${port}/ws`;
            this.socket = new WebSocket(url);

            this.socket.onopen = () => resolve();
            this.socket.onerror = () => reject(new Error(`No se pudo abrir ${url}`));
            this.socket.onmessage = (ev) => {
                // El firmware manda una trama por linea; el WebSocket puede agrupar.
                String(ev.data).split('\n').forEach((l) => this.onLine(l));
            };
            this.socket.onclose = () => this.onClose();
        });
    }

    async send(text) {
        if (this.socket?.readyState !== WebSocket.OPEN) throw new Error('WebSocket cerrado');
        this.socket.send(text);
    }

    async close() {
        this.socket?.close();
        this.socket = null;
    }
}

/**
 * Simulador. Habla el mismo protocolo que el firmware, asi que el resto de la
 * aplicacion no distingue si hay un rover al otro lado.
 */
class DemoTransport {
    constructor(onLine) {
        this.onLine = onLine;
        this.armed = false;
        this.motors = new Array(CONFIG.MOTOR_COUNT).fill(0);
        this.stepperPos = 0;
        this.uptime = 0;
        this.seq = 0;
        this.rxOk = 0;
        this.timer = null;
    }

    async open() {
        this.timer = setInterval(() => this._tick(), 1000 / CONFIG.TX_HZ);
    }

    _tick() {
        this.uptime += 1 / CONFIG.TX_HZ;
        const batteryV = 11.85 + Math.sin(this.uptime) * 0.05;

        this.onLine(JSON.stringify({
            type: 'telemetry',
            seq: this.seq,
            battery_v: Number(batteryV.toFixed(2)),
            raw_mv: Math.round((batteryV / 4.3) * 1000),
            uptime: Math.floor(this.uptime),
            armed: this.armed,
            motors: this.motors.map((v) => Math.round(v)),
            stepper_pos: this.stepperPos,
            stepper_moving: false,
            rx_ok: this.rxOk,
            rx_err: 0,
        }));
    }

    async send(text) {
        for (const line of text.split('\n')) {
            if (!line.trim()) continue;
            let cmd;
            try { cmd = JSON.parse(line); } catch (e) { continue; }

            this.rxOk++;
            if (typeof cmd.seq === 'number') this.seq = cmd.seq;

            switch (cmd.action) {
                case 'ARM': this.armed = true; break;
                case 'STOP':
                case 'DISARM':
                    this.armed = false;
                    this.motors.fill(0);
                    break;
                case 'DRIVE': {
                    if (!this.armed) break;
                    const left = clamp(cmd.linear + cmd.angular, -100, 100);
                    const right = clamp(cmd.linear - cmd.angular, -100, 100);
                    this.motors = [left, right, left, right, left, right];
                    break;
                }
                case 'TEST_MOTOR':
                    this.motors[cmd.motor] = cmd.speed;
                    break;
                case 'STEPPER_JOG':
                    this.stepperPos += cmd.steps;
                    break;
                default: break;                 // HEARTBEAT, SERVO_SET: sin efecto visible
            }
        }
    }

    async close() {
        clearInterval(this.timer);
        this.timer = null;
    }
}

/* ========================================================================= */
/* Link: cola de salida y recepcion                                          */
/* ========================================================================= */

const Link = {
    transport: null,
    lastRxAt: 0,

    /*
     * Dos carriles de salida con un unico escritor en vuelo:
     *
     *   events  ARM, DISARM, STEPPER_JOG... no se pueden perder -> cola FIFO
     *   frame   DRIVE, TEST_MOTOR, HEARTBEAT: son instantaneas de estado, si
     *           una se retrasa lo correcto es sustituirla, no encolarla
     *
     * La version anterior tenia dos setInterval escribiendo a la vez sobre el
     * mismo writer sin await. Cuando el puerto aplicaba contrapresion las
     * promesas se acumulaban y los comandos llegaban cada vez mas tarde: ese
     * era el desfase que se veia crecer con el tiempo en la prueba de motores.
     */
    events: [],
    frame: null,
    writing: false,

    async connect(transport) {
        this.transport = transport;
        this.events = [];
        this.frame = null;
        this.writing = false;
        this.lastRxAt = 0;
    },

    async disconnect() {
        const t = this.transport;
        this.transport = null;
        this.events = [];
        this.frame = null;
        if (t) await t.close();
    },

    get connected() { return this.transport !== null; },

    sendEvent(obj) {
        if (!this.transport) return;
        if (this.events.length >= CONFIG.MAX_QUEUED_EVENTS) this.events.shift();
        this.events.push(obj);
        this._pump();
    },

    sendFrame(obj) {
        if (!this.transport) return;
        this.frame = obj;
        this._pump();
    },

    async _pump() {
        if (this.writing || !this.transport) return;
        if (this.events.length === 0 && !this.frame) return;

        const batch = this.events;
        this.events = [];
        if (this.frame) { batch.push(this.frame); this.frame = null; }

        const payload = batch.map((o) => JSON.stringify(o)).join('\n') + '\n';
        View.setDebugTx(payload.trim());

        this.writing = true;
        try {
            await this.transport.send(payload);
        } catch (err) {
            console.error('Fallo de transmision:', err);
            App.handleTransportClosed();
            return;
        } finally {
            this.writing = false;
        }

        // Mientras se escribia pudo llegar mas trabajo.
        if (this.events.length > 0 || this.frame) this._pump();
    },

    handleLine(rawLine) {
        const line = rawLine.trim();
        if (!line) return;

        /*
         * En modo cable los ESP_LOG del firmware comparten UART con la
         * telemetria. Todo lo que no empiece por '{' es un log, no una trama:
         * se ignora en vez de ensuciar la consola con errores de parseo.
         */
        if (line[0] !== '{') {
            console.debug('[rover]', line);
            return;
        }

        let data;
        try {
            data = JSON.parse(line);
        } catch (e) {
            return;                             // trama partida, la siguiente llegara entera
        }
        if (data.type !== 'telemetry') return;

        this.lastRxAt = performance.now();
        View.setDebugRx(line);
        applyTelemetry(data);
    },
};

/* ========================================================================= */
/* Telemetria entrante                                                       */
/* ========================================================================= */

function applyTelemetry(data) {
    const t = State.telemetry;

    t.batteryV = num(data.battery_v);
    t.rawMv = num(data.raw_mv);
    t.uptime = num(data.uptime);
    t.stepperPos = num(data.stepper_pos);
    t.stepperMoving = Boolean(data.stepper_moving);
    t.rxOk = num(data.rx_ok);
    t.rxErr = num(data.rx_err);

    if (Array.isArray(data.motors)) {
        for (let i = 0; i < CONFIG.MOTOR_COUNT; i++) t.motors[i] = num(data.motors[i]);
    }

    /*
     * El firmware manda su estado de armado y manda. Si su failsafe desarmo el
     * rover por perdida de enlace, el panel tiene que enterarse: antes se
     * quedaba mostrando ARMADO mientras la placa ya estaba parada.
     */
    if (typeof data.armed === 'boolean' && data.armed !== State.armed) {
        State.armed = data.armed;
        View.renderArmState();
        if (!data.armed) View.flashNotice('El rover se desarmo (failsafe del firmware).');
    }

    // RTT real: el firmware devuelve el ultimo seq recibido.
    if (typeof data.seq === 'number') {
        const sentAt = State.pendingPings.get(data.seq);
        if (sentAt !== undefined) {
            State.link.latency = Math.round(performance.now() - sentAt);
            State.pendingPings.clear();         // los anteriores ya no interesan
        }
    }
}

/* ========================================================================= */
/* Entradas: teclado y mando                                                 */
/* ========================================================================= */

const Input = {
    gamepadConnected: false,

    handleKey(e, down) {
        const code = e.code;
        if (!['KeyW', 'KeyA', 'KeyS', 'KeyD'].includes(code)) return;
        if (e.repeat) return;

        State.keys[code[3].toLowerCase()] = down;
        View.setInputChip('hud-wasd', Object.values(State.keys).some(Boolean));
    },

    releaseAll() {
        State.keys = { w: false, a: false, s: false, d: false };
        View.setInputChip('hud-wasd', false);
    },

    /** Calcula linear/angular. El mando tiene prioridad sobre el teclado. */
    computeDrive() {
        let left = 0;
        let right = 0;

        const { w, a, s, d } = State.keys;
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

        State.drive.linear = (left + right) / 2;
        State.drive.angular = (left - right) / 2;
    },
};

/* ========================================================================= */
/* Lazo de transmision: el unico sitio que emite tramas periodicas           */
/* ========================================================================= */

const Tx = {
    timer: null,

    start() {
        this.stop();
        this.timer = setInterval(() => this.tick(), 1000 / CONFIG.TX_HZ);
    },

    stop() {
        if (this.timer) clearInterval(this.timer);
        this.timer = null;
    },

    tick() {
        if (!Link.connected) return;

        // Enlace caido: sin telemetria reciente, no se pinta latencia vieja.
        const stale = Link.lastRxAt === 0 ||
                      (performance.now() - Link.lastRxAt) > CONFIG.STALE_MS;
        if (stale !== State.link.stale) {
            State.link.stale = stale;
            if (stale) State.link.latency = null;
            View.renderLinkState();
        }

        const seq = ++State.seq;
        State.pendingPings.set(seq, performance.now());
        if (State.pendingPings.size > 64) State.pendingPings.clear();

        /*
         * Exactamente una trama por ciclo, elegida por prioridad. Antes habia
         * dos temporizadores compitiendo (latido cada 100 ms y prueba de motor
         * cada 50 ms) que podian mandar ordenes contradictorias en el mismo
         * milisegundo.
         */
        if (State.manual) {
            Link.sendFrame({
                action: 'TEST_MOTOR',
                motor: State.manual.motor,
                speed: State.manual.speed,
                seq,
            });
        } else if (State.armed) {
            Input.computeDrive();
            Link.sendFrame({
                action: 'DRIVE',
                linear: round1(State.drive.linear),
                angular: round1(State.drive.angular),
                seq,
            });
        } else {
            Link.sendFrame({ action: 'HEARTBEAT', seq });
        }

        View.renderTelemetry();
    },
};

/* ========================================================================= */
/* Vista                                                                     */
/* ========================================================================= */

const View = {
    el: {},
    noticeTimer: null,

    cache() {
        const ids = [
            'login-screen', 'login-status', 'rover-ip', 'rover-port',
            'status-badge', 'arm-badge', 'latency-badge', 'btn-arm-toggle',
            'btn-preflight', 'btn-disconnect',
            'hud-wasd', 'hud-gamepad', 'notice-bar',
            'debug-tx-data', 'debug-rx-data', 'debug-link-status',
            'debug-rx-ok', 'debug-rx-err',
            'adc-mv-val', 'adc-pin-v', 'battery-val',
            'stepper-pos-val', 'stepper-state-badge',
            'view-general', 'view-diagnostics', 'tab-general', 'tab-diagnostics',
            'motor-test-popover', 'popover-title',
            'calibration-modal', 'modal-step-badge', 'modal-step-title', 'modal-step-desc',
            'pin-i2c-badge', 'pin-adc-badge',
        ];
        for (const id of ids) this.el[id] = document.getElementById(id);

        this.motorBadges = [];
        this.wheels = [];
        this.wheelVals = [];
        this.servoBadges = [];
        for (let i = 0; i < CONFIG.MOTOR_COUNT; i++) {
            this.motorBadges[i] = document.getElementById(`pin-m${i + 1}-badge`);
            this.wheels[i] = document.getElementById(`m${i + 1}`);
            this.wheelVals[i] = this.wheels[i]?.querySelector('.wheel-val');
        }
        for (let s = 0; s < CONFIG.STEERING_CHANNELS; s++) {
            this.servoBadges[s] = document.getElementById(`servo-dir-${s}-badge`);
        }
    },

    setBadge(id, text, cls) {
        const el = this.el[id];
        if (!el) return;
        el.textContent = text;
        el.className = `badge ${cls}`;
    },

    setInputChip(id, active) {
        this.el[id]?.classList.toggle('active', active);
    },

    setDebugTx(text) { if (this.el['debug-tx-data']) this.el['debug-tx-data'].textContent = text; },
    setDebugRx(text) { if (this.el['debug-rx-data']) this.el['debug-rx-data'].textContent = text; },

    flashNotice(message) {
        const bar = this.el['notice-bar'];
        if (!bar) return;
        bar.textContent = message;
        bar.classList.add('visible');
        clearTimeout(this.noticeTimer);
        this.noticeTimer = setTimeout(() => bar.classList.remove('visible'), 4000);
    },

    renderLinkState() {
        const { mode, stale, latency } = State.link;

        const labels = {
            serial: '🔌 CABLE USB',
            ws: '📡 WI-FI',
            demo: '🧪 SIMULACION',
            none: '● DESCONECTADO',
        };

        if (mode === 'none') {
            this.setBadge('status-badge', labels.none, 'badge-danger');
            this.setBadge('latency-badge', 'Latencia: --', 'badge-gray');
        } else if (stale) {
            this.setBadge('status-badge', `${labels[mode]} · SIN DATOS`, 'badge-warning');
            this.setBadge('latency-badge', 'Latencia: --', 'badge-gray');
        } else {
            this.setBadge('status-badge', labels[mode], 'badge-success');
            this.renderLatency();
        }

        const dbg = this.el['debug-link-status'];
        if (dbg) {
            dbg.textContent = mode === 'none' ? 'Desconectado'
                            : stale ? `${labels[mode]} (sin telemetria)`
                            : labels[mode];
            dbg.dataset.state = mode === 'none' ? 'off' : stale ? 'warn' : 'ok';
        }

        const connected = mode !== 'none';
        if (this.el['btn-arm-toggle']) this.el['btn-arm-toggle'].disabled = !connected;
        if (this.el['btn-preflight']) this.el['btn-preflight'].disabled = !connected;
        if (this.el['btn-disconnect']) this.el['btn-disconnect'].hidden = !connected;

        const i2c = this.el['pin-i2c-badge'];
        if (i2c) {
            const live = connected && !stale;
            i2c.className = live ? 'pin-badge pin-i2c' : 'pin-badge pin-idle';
            i2c.textContent = live ? 'ACTIVO 50 Hz' : 'SIN ENLACE';
        }
    },

    renderLatency() {
        const { mode, stale, latency } = State.link;
        if (mode === 'none' || stale || latency === null) {
            this.setBadge('latency-badge', 'Latencia: --', 'badge-gray');
            return;
        }
        this.setBadge('latency-badge', `Latencia: ${latency} ms`,
                      latency > 150 ? 'badge-warning' : 'badge-gray');
    },

    renderArmState() {
        const armed = State.armed;
        this.setBadge('arm-badge', armed ? 'ARMADO' : 'DESARMADO',
                      armed ? 'badge-success' : 'badge-danger');

        const btn = this.el['btn-arm-toggle'];
        if (btn) {
            btn.textContent = armed ? '🛡️ DESARMAR ROVER' : '🛡️ ARMAR ROVER';
            btn.classList.toggle('armed', armed);
        }
    },

    renderTelemetry() {
        const t = State.telemetry;

        this.renderLatency();
        setText(this.el['battery-val'], `${t.batteryV.toFixed(2)} V`);
        setText(this.el['adc-mv-val'], `${Math.round(t.rawMv)} mV`);
        setText(this.el['adc-pin-v'], `${(t.rawMv / 1000).toFixed(2)} V`);
        setText(this.el['debug-rx-ok'], String(t.rxOk));
        setText(this.el['debug-rx-err'], String(t.rxErr));

        const errEl = this.el['debug-rx-err'];
        if (errEl) errEl.dataset.state = t.rxErr > 0 ? 'warn' : 'ok';

        for (let i = 0; i < CONFIG.MOTOR_COUNT; i++) {
            const spd = t.motors[i];
            const active = Math.abs(spd) > 1;

            const badge = this.motorBadges[i];
            if (badge) {
                badge.className = active ? 'pin-badge pin-active' : 'pin-badge pin-idle';
                badge.textContent = active ? `${Math.round(spd)} %` : 'INACTIVO';
            }

            setText(this.wheelVals[i], `${Math.round(spd)}%`);

            const wheel = this.wheels[i];
            if (wheel) {
                wheel.classList.toggle('active', active);
                wheel.classList.toggle('reverse', spd < -1);
                wheel.style.setProperty('--wheel-power', `${Math.abs(spd)}%`);
            }
        }

        for (let s = 0; s < CONFIG.STEERING_CHANNELS; s++) {
            const badge = this.servoBadges[s];
            if (!badge) continue;
            const ang = State.servoAngles[s];
            badge.textContent = steerLabel(ang);
            badge.className = (ang < 80 || ang > 100) ? 'pin-badge pin-active' : 'pin-badge pin-i2c';
        }

        setText(this.el['stepper-pos-val'], String(t.stepperPos));
        const st = this.el['stepper-state-badge'];
        if (st) {
            st.className = t.stepperMoving ? 'pin-badge pin-active' : 'pin-badge pin-idle';
            st.textContent = t.stepperMoving ? 'EN MOVIMIENTO' : 'REPOSO';
        }

        const adc = this.el['pin-adc-badge'];
        if (adc) {
            const ok = t.rawMv > 0;
            adc.className = ok ? 'pin-badge pin-active' : 'pin-badge pin-idle';
            adc.textContent = ok ? `${Math.round(t.rawMv)} mV` : 'SIN LECTURA';
        }
    },

    showLogin(show) {
        if (this.el['login-screen']) this.el['login-screen'].style.display = show ? 'flex' : 'none';
    },

    setLoginStatus(text, kind) {
        const el = this.el['login-status'];
        if (!el) return;
        el.textContent = text;
        el.dataset.kind = kind || '';
    },

    switchMode(mode) {
        const general = mode === 'general';
        this.el['view-general']?.classList.toggle('hidden', !general);
        this.el['view-diagnostics']?.classList.toggle('hidden', general);
        this.el['tab-general']?.classList.toggle('active', general);
        this.el['tab-diagnostics']?.classList.toggle('active', !general);
    },
};

/* ========================================================================= */
/* Asistente de pre-vuelo                                                    */
/* ========================================================================= */

/*
 * Antes eran setTimeout sueltos: si el operario pulsaba "Si" rapido, los
 * temporizadores del paso anterior seguian vivos y pisaban los comandos del
 * paso nuevo. Ahora cada ejecucion lleva su token y se cancela sola.
 */
const Wizard = {
    step: 0,
    token: 0,
    active: false,
    resolveAnswer: null,

    get totalSteps() { return 1 + CONFIG.STEERING_CHANNELS + CONFIG.MOTOR_COUNT; },

    async start() {
        const token = ++this.token;
        this.step = 0;
        this.active = true;

        // El pre-vuelo mueve servos y motores uno a uno: nunca con el rover armado.
        if (State.armed) App.disarm();

        View.el['calibration-modal'].style.display = 'flex';
        this._setBusy(false);

        const completed = await this._run(token);

        // Si el token cambio, otra ejecucion o una cancelacion tomo el relevo y
        // ya se encargo de cerrar el modal y de parar el hardware.
        if (this.token !== token) return;

        this.active = false;
        State.manual = null;
        this._setBusy(false);
        View.el['calibration-modal'].style.display = 'none';

        if (completed) {
            View.flashNotice('Pre-vuelo completado. Rover armado.');
            App.arm();
        }
    },

    async _run(token) {
        const alive = () => this.token === token;

        this._render(1, 'Enlace y bateria',
            `Enlace: <strong>${State.link.mode.toUpperCase()}</strong> · ` +
            `Bateria: <strong>${State.telemetry.batteryV.toFixed(2)} V</strong><br><br>` +
            `Se probaran ${CONFIG.STEERING_CHANNELS} servos y ${CONFIG.MOTOR_COUNT} motores. ` +
            `Aparta las manos del chasis antes de continuar.`);
        if (!await this._await(token)) return false;

        for (let ch = 0; ch < CONFIG.STEERING_CHANNELS; ch++) {
            if (!alive()) return false;
            this._render(2 + ch, `Servo ${ch + 1} (canal ${ch})`,
                'Barrido izquierda 45° → derecha 135° → centro 90°. ¿Se movio en ambos sentidos?');

            this._setBusy(true);
            for (const angle of [45, 135, 90]) {
                if (!alive()) return false;
                App.setServo(ch, angle);
                await sleep(600);
            }
            this._setBusy(false);
            if (!await this._await(token)) return false;
        }

        for (let m = 0; m < CONFIG.MOTOR_COUNT; m++) {
            if (!alive()) return false;
            this._render(2 + CONFIG.STEERING_CHANNELS + m, `Motor ${MOTOR_NAMES[m]}`,
                `Impulso de avance y retroceso al ${CONFIG.WIZARD_SPEED} %. ¿Giro en ambos sentidos?`);

            this._setBusy(true);
            for (const speed of [CONFIG.WIZARD_SPEED, -CONFIG.WIZARD_SPEED, 0]) {
                if (!alive()) { State.manual = null; return false; }
                if (speed === 0) {
                    State.manual = null;
                    Link.sendEvent({ action: 'TEST_MOTOR', motor: m, speed: 0 });
                } else {
                    State.manual = { motor: m, speed };
                }
                await sleep(700);
            }
            this._setBusy(false);
            if (!await this._await(token)) return false;
        }

        return true;
    },

    /** Bloquea "Si" mientras se mueve un actuador; "Cancelar" nunca se bloquea. */
    _setBusy(busy) {
        const yes = document.getElementById('btn-wizard-yes');
        if (!yes) return;
        yes.disabled = busy;
        yes.textContent = busy ? 'Probando...' : 'Sí, confirmar';
    },

    _render(step, title, html) {
        this.step = step;
        View.el['modal-step-badge'].textContent = `PASO ${step} DE ${this.totalSteps}`;
        View.el['modal-step-title'].textContent = title;
        View.el['modal-step-desc'].innerHTML = html;
    },

    /** Espera la respuesta del operario. false si cancela o si el token caduca. */
    _await(token) {
        return new Promise((resolve) => {
            this.resolveAnswer = (answer) => {
                this.resolveAnswer = null;
                resolve(this.token === token && answer);
            };
        });
    },

    answer(confirmed) {
        if (confirmed) {
            this.resolveAnswer?.(true);         // se ignora si hay un actuador en marcha
            return;
        }
        this.cancel('Pre-vuelo cancelado. El rover sigue desarmado.');
    },

    /**
     * Cancelacion inmediata. No pasa por _await a proposito: durante un barrido
     * de servo o un impulso de motor no hay nadie esperando respuesta, y el
     * boton de cancelar tiene que funcionar justo en ese momento.
     */
    cancel(notice) {
        if (!this.active) return;
        this.token++;                            // invalida la ejecucion en curso
        this.resolveAnswer?.(false);
        this.resolveAnswer = null;
        this.active = false;

        this._setBusy(false);
        if (View.el['calibration-modal']) View.el['calibration-modal'].style.display = 'none';

        App.emergencyStop();
        if (notice) View.flashNotice(notice);
    },

    /** Cancelacion silenciosa al desconectar. */
    abort() {
        if (!this.active) return;
        this.token++;
        this.resolveAnswer?.(false);
        this.resolveAnswer = null;
        this.active = false;
        this._setBusy(false);
        State.manual = null;
        if (View.el['calibration-modal']) View.el['calibration-modal'].style.display = 'none';
    },
};

/* ========================================================================= */
/* Aplicacion                                                                */
/* ========================================================================= */

const App = {

    init() {
        View.cache();
        View.renderLinkState();
        View.renderArmState();
        this.bindEvents();
    },

    bindEvents() {
        window.addEventListener('keydown', (e) => {
            if (isTypingTarget(e.target)) return;
            if (e.code === 'Escape') { this.emergencyStop(); return; }
            if (e.code === 'Space') { e.preventDefault(); this.emergencyStop(); return; }
            Input.handleKey(e, true);
        });
        window.addEventListener('keyup', (e) => {
            if (isTypingTarget(e.target)) return;
            Input.handleKey(e, false);
        });

        // Al perder el foco se sueltan las teclas: si no, una tecla "pegada"
        // dejaria al rover avanzando solo.
        window.addEventListener('blur', () => Input.releaseAll());

        window.addEventListener('gamepadconnected', () => View.setInputChip('hud-gamepad', true));
        window.addEventListener('gamepaddisconnected', () => View.setInputChip('hud-gamepad', false));

        // Parada segura si se cierra la pestana con el rover armado.
        window.addEventListener('beforeunload', () => {
            if (State.armed || State.manual) this.emergencyStop();
        });

        this.bindMotorPopover();
    },

    /* --- Conexion --- */

    async connectSerial() {
        View.setLoginStatus('Solicitando puerto serie...', 'info');
        const transport = new SerialTransport(
            (line) => Link.handleLine(line),
            () => this.handleTransportClosed());
        try {
            await transport.open();
            await this.onConnected(transport, 'serial');
        } catch (err) {
            View.setLoginStatus(err.message || 'No se pudo abrir el puerto serie.', 'error');
        }
    },

    async connectWifi() {
        const ip = View.el['rover-ip'].value.trim() || '192.168.4.1';
        const port = View.el['rover-port'].value.trim() || '8080';
        View.setLoginStatus(`Conectando con ${ip}:${port}...`, 'info');

        const transport = new WsTransport(
            (line) => Link.handleLine(line),
            () => this.handleTransportClosed());
        try {
            await transport.open(ip, port);
            await this.onConnected(transport, 'ws');
        } catch (err) {
            View.setLoginStatus('Sin respuesta. Revisa la IP o conecta por cable.', 'error');
        }
    },

    async connectDemo() {
        const transport = new DemoTransport((line) => Link.handleLine(line));
        await transport.open();
        await this.onConnected(transport, 'demo');
    },

    async onConnected(transport, mode) {
        // Cierra cualquier enlace previo: si no, su lector y su temporizador
        // seguirian vivos y habria dos pipelines escribiendo a la vez.
        Tx.stop();
        Wizard.abort();
        await Link.disconnect();

        await Link.connect(transport);
        State.link.mode = mode;
        State.link.stale = true;
        State.armed = false;

        View.showLogin(false);
        View.setLoginStatus('', '');
        View.renderLinkState();
        View.renderArmState();

        document.activeElement?.blur();          // que WASD llegue a window
        window.focus();

        Tx.start();
    },

    async disconnect() {
        Wizard.abort();
        this.emergencyStop();
        Tx.stop();
        await Link.disconnect();
        this.handleTransportClosed();
    },

    handleTransportClosed() {
        if (State.link.mode === 'none') return;

        Tx.stop();
        Link.transport = null;
        State.link.mode = 'none';
        State.link.stale = true;
        State.link.latency = null;
        State.armed = false;
        State.manual = null;

        View.renderLinkState();
        View.renderArmState();
        View.showLogin(true);
        View.setLoginStatus('Enlace cerrado.', 'error');
    },

    /* --- Armado --- */

    arm() {
        State.armed = true;
        Link.sendEvent({ action: 'ARM' });
        View.renderArmState();
    },

    disarm() {
        State.armed = false;
        State.manual = null;
        Link.sendEvent({ action: 'DISARM' });
        View.renderArmState();
    },

    toggleArm() {
        State.armed ? this.disarm() : this.arm();
    },

    /** Corte total: motores, paso a paso y armado. Tecla Escape o barra espaciadora. */
    emergencyStop() {
        State.armed = false;
        State.manual = null;
        Input.releaseAll();
        Link.sendEvent({ action: 'STOP' });
        View.renderArmState();
        View.flashNotice('PARADA DE EMERGENCIA enviada.');
    },

    /* --- Diagnostico --- */

    /**
     * Entra en prueba manual de un motor.
     *
     * Mientras dura, el lazo TX deja de emitir DRIVE, asi que el firmware
     * conservaria la ultima consigna de conduccion y las otras cinco ruedas
     * seguirian girando. Por eso se desarma primero: una prueba individual
     * solo tiene sentido con el rover quieto.
     */
    beginManualTest(motor, speed) {
        if (State.armed) {
            this.disarm();
            View.flashNotice('Rover desarmado para ejecutar la prueba individual.');
        }
        State.manual = { motor: Number(motor), speed: Number(speed) };
    },

    /** Deslizador de motor del panel de diagnostico. */
    setMotor(index, value) {
        const speed = Number(value);
        const el = document.querySelector(`.motor-control-item[data-motor="${index}"] .val-display`);
        if (el) el.textContent = `${speed}%`;

        if (speed === 0) {
            State.manual = null;
            Link.sendEvent({ action: 'TEST_MOTOR', motor: Number(index), speed: 0 });
        } else {
            this.beginManualTest(index, speed);
        }
    },

    stopAllMotors() {
        State.manual = null;
        document.querySelectorAll('.motor-control-item input[type="range"]').forEach((r) => { r.value = 0; });
        document.querySelectorAll('.motor-control-item .val-display').forEach((d) => { d.textContent = '0%'; });
        for (let i = 0; i < CONFIG.MOTOR_COUNT; i++) {
            Link.sendEvent({ action: 'TEST_MOTOR', motor: i, speed: 0 });
        }
    },

    setServo(channel, value) {
        const angle = Number(value);
        const ch = Number(channel);
        State.servoAngles[ch] = angle;

        const label = document.getElementById(`servo-${ch}-val`);
        if (label) label.textContent = steerLabel(angle);

        Link.sendEvent({ action: 'SERVO_SET', channel: ch, angle });
    },

    jogStepper(steps) {
        Link.sendEvent({ action: 'STEPPER_JOG', steps: Number(steps), speed: 1000 });
    },

    /* --- Prueba rapida desde el esquema del rover --- */

    bindMotorPopover() {
        for (let i = 0; i < CONFIG.MOTOR_COUNT; i++) {
            const wheel = View.wheels[i];
            if (!wheel) continue;
            wheel.addEventListener('click', (e) => {
                e.stopPropagation();
                this.openMotorPopover(i, wheel);
            });
        }

        document.addEventListener('click', () => this.closeMotorPopover());
        View.el['motor-test-popover']?.addEventListener('click', (e) => e.stopPropagation());

        const hold = (btnId, sign) => {
            const btn = document.getElementById(btnId);
            if (!btn) return;

            const start = (e) => {
                e.preventDefault();
                if (this.popoverMotor === null) return;
                this.beginManualTest(this.popoverMotor, sign * CONFIG.TEST_SPEED);
            };
            const stop = (e) => {
                e.preventDefault();
                this.releaseMotorTest();
            };

            btn.addEventListener('mousedown', start);
            btn.addEventListener('touchstart', start, { passive: false });
            btn.addEventListener('mouseup', stop);
            btn.addEventListener('mouseleave', stop);
            btn.addEventListener('touchend', stop);
            btn.addEventListener('touchcancel', stop);
        };

        hold('btn-popover-forward', 1);
        hold('btn-popover-backward', -1);
    },

    popoverMotor: null,

    openMotorPopover(index, wheelEl) {
        this.releaseMotorTest();
        this.popoverMotor = index;

        const popover = View.el['motor-test-popover'];
        const container = document.querySelector('.rover-view');
        if (!popover || !container) return;

        View.el['popover-title'].textContent = MOTOR_NAMES[index];

        const wheelRect = wheelEl.getBoundingClientRect();
        const boxRect = container.getBoundingClientRect();
        const top = clamp(wheelRect.top - boxRect.top + wheelRect.height / 2 - 60,
                          10, boxRect.height - 120);
        const left = clamp(wheelRect.left - boxRect.left + wheelRect.width / 2 - 85,
                           10, boxRect.width - 180);

        popover.style.top = `${top}px`;
        popover.style.left = `${left}px`;
        popover.classList.remove('hidden');
    },

    closeMotorPopover() {
        this.releaseMotorTest();
        this.popoverMotor = null;
        View.el['motor-test-popover']?.classList.add('hidden');
    },

    /** Suelta la prueba manual y manda un cero explicito (no depende del failsafe). */
    releaseMotorTest() {
        if (!State.manual) return;
        const motor = State.manual.motor;
        State.manual = null;
        Link.sendEvent({ action: 'TEST_MOTOR', motor, speed: 0 });
    },

    /* --- Pre-vuelo --- */

    startPreFlight() { Wizard.start(); },
    answerPreFlight(confirmed) { Wizard.answer(confirmed); },

    switchMode(mode) { View.switchMode(mode); },
};

/* ========================================================================= */
/* Utilidades                                                                */
/* ========================================================================= */

function clamp(v, lo, hi) { return Math.min(hi, Math.max(lo, v)); }
function round1(v) { return Math.round(v * 10) / 10; }
function num(v) { return typeof v === 'number' && Number.isFinite(v) ? v : 0; }
function sleep(ms) { return new Promise((r) => setTimeout(r, ms)); }
function setText(el, text) { if (el && el.textContent !== text) el.textContent = text; }

function steerLabel(angle) {
    const a = Math.round(angle);
    if (a < 80) return `⬅️ IZQ (${a}°)`;
    if (a > 100) return `➡️ DER (${a}°)`;
    return `⏺️ CENTRO (${a}°)`;
}

function isTypingTarget(target) {
    return target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement;
}

document.addEventListener('DOMContentLoaded', () => App.init());
