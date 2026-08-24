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
    STEERING_CHANNELS: 4,       // un servo por esquina: FL, FR, RL, RR
    // Las 6 ruedas van en paralelo a un solo canal BTS7960: el esquema las
    // muestra todas, pero solo hay una consigna de traccion.
    MOTOR_COUNT: 6,
};

/*
 * Identidad de este panel, valida mientras dure la pestana. El firmware concede
 * el mando al primer cliente que actua y rechaza al resto, para que dos paneles
 * conectados a la vez no manden consignas contradictorias a 20 Hz.
 */
const CLIENT_ID = 'p' + Math.random().toString(36).slice(2, 10);

const DRIVE_NAME = 'Tracción (M1–M6)';

const SERVO_NAMES = [
    'FL (Delantero Izq)', 'FR (Delantero Der)',
    'RL (Trasero Izq)', 'RR (Trasero Der)',
];

/* ========================================================================= */
/* Estado                                                                    */
/* ========================================================================= */

const State = {
    link: { mode: 'none', connected: false, stale: true, latency: null },
    armed: false,
    // '' = mando libre. Distinto de CLIENT_ID = otro operador esta al mando.
    owner: '',
    keys: { w: false, a: false, s: false, d: false },
    drive: { linear: 0, angular: 0 },

    // Prueba manual de un motor. null = sin prueba activa.
    // Mientras no sea null, el lazo TX manda TEST_MOTOR en vez de DRIVE.
    manual: null,

    telemetry: {
        uptime: 0,
        motors: new Array(CONFIG.MOTOR_COUNT).fill(0),
        // Angulos reales de consigna, publicados por el firmware. No se calculan
        // aqui: con varios clientes conectados cada uno veria algo distinto.
        servos: new Array(CONFIG.STEERING_CHANNELS).fill(90),
        rxOk: 0, rxErr: 0,
        clients: 0, heap: 0,
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
        this.servos = new Array(CONFIG.STEERING_CHANNELS).fill(90);
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

        this.onLine(JSON.stringify({
            type: 'telemetry',
            seq: this.seq,
            uptime: Math.floor(this.uptime),
            armed: this.armed,
            motors: this.motors.map((v) => Math.round(v)),
            servos: this.servos.map((v) => Math.round(v)),
            rx_ok: this.rxOk,
            rx_err: 0,
            clients: 1,
            heap: 280000,
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
                    // Un solo canal de traccion y direccion 4WS coordinada:
                    // delanteros y traseros giran en sentido opuesto.
                    this.motors.fill(clamp(cmd.linear, -100, 100));
                    const delta = clamp((cmd.angular / 100) * 45, -45, 45);
                    this.servos[0] = this.servos[1] = 90 + delta;
                    this.servos[2] = this.servos[3] = 90 - delta;
                    break;
                }
                case 'TEST_MOTOR':
                    this.motors.fill(cmd.speed);    // el indice no distingue nada
                    break;
                case 'SERVO_SET':
                    if (cmd.channel < this.servos.length) this.servos[cmd.channel] = cmd.angle;
                    break;
                default: break;                 // HEARTBEAT: sin efecto visible
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
     *   events  ARM, DISARM, SERVO_SET... no se pueden perder -> cola FIFO
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
        this.events.push({ ...obj, client: CLIENT_ID });
        this._pump();
    },

    sendFrame(obj) {
        if (!this.transport) return;
        this.frame = { ...obj, client: CLIENT_ID };
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

    t.uptime = num(data.uptime);
    t.rxOk = num(data.rx_ok);
    t.rxErr = num(data.rx_err);
    t.clients = num(data.clients);
    t.heap = num(data.heap);

    if (Array.isArray(data.motors)) {
        for (let i = 0; i < CONFIG.MOTOR_COUNT; i++) t.motors[i] = num(data.motors[i]);
    }
    if (Array.isArray(data.servos)) {
        for (let i = 0; i < CONFIG.STEERING_CHANNELS; i++) t.servos[i] = num(data.servos[i]);
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

    // Arbitraje del mando: si otro panel lo tiene, este pasa a observador.
    if (typeof data.owner === 'string' && data.owner !== State.owner) {
        const hadControl = hasControl();
        State.owner = data.owner;
        View.renderArmState();
        if (hadControl && !hasControl()) {
            State.manual = null;
            View.flashNotice('Otro operador tomo el mando. Este panel queda en observacion.');
        } else if (!hadControl && hasControl()) {
            View.flashNotice('Mando disponible para este panel.');
        }
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

    /**
     * Calcula linear/angular como ejes INDEPENDIENTES: W/S es solo traccion y
     * A/D es solo direccion (los servos 4WS ponen el angulo, la velocidad no
     * cambia al girar). La mezcla diferencial de la version skid-steer ya no
     * aplica: con un unico canal de motores generaba angulos fantasma al
     * combinar teclas. El mando tiene prioridad sobre el teclado, eje a eje.
     */
    computeDrive() {
        let linear = 0;
        let angular = 0;

        const { w, a, s, d } = State.keys;
        if (w !== s) linear = w ? 100 : -100;
        if (a !== d) angular = a ? -100 : 100;   // negativo = izquierda

        const gp = navigator.getGamepads?.()[0];
        if (gp) {
            const rawLin = -gp.axes[1];
            const rawAng = gp.axes[0];
            if (Math.abs(rawLin) > 0.12) linear = clamp(rawLin * 100, -100, 100);
            if (Math.abs(rawAng) > 0.12) angular = clamp(rawAng * 100, -100, 100);
        }

        State.drive.linear = linear;
        State.drive.angular = angular;
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
        if (!hasControl()) {
            // Observador: solo late. Mandar DRIVE seria inutil (el firmware lo
            // rechaza) y ademas mantendria vivo el failsafe de otro operador.
            Link.sendFrame({ action: 'HEARTBEAT', seq });
        } else if (State.manual) {
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
            'status-badge', 'arm-badge', 'latency-badge', 'control-badge',
            'btn-arm-toggle', 'btn-preflight', 'btn-disconnect',
            'hud-wasd', 'hud-gamepad', 'notice-bar',
            'debug-tx-data', 'debug-rx-data', 'debug-link-status',
            'debug-rx-ok', 'debug-rx-err',
            'uptime-val', 'heap-val', 'clients-val',
            'view-general', 'view-diagnostics', 'tab-general', 'tab-diagnostics',
            'motor-test-popover', 'popover-title',
            'calibration-modal', 'modal-step-badge', 'modal-step-title', 'modal-step-desc',
            'pin-i2c-badge', 'pin-drive-badge',
        ];
        for (const id of ids) this.el[id] = document.getElementById(id);

        this.wheels = [];
        this.wheelVals = [];
        this.servoBadges = [];
        this.servoLabels = [];
        for (let i = 0; i < CONFIG.MOTOR_COUNT; i++) {
            this.wheels[i] = document.getElementById(`m${i + 1}`);
            this.wheelVals[i] = this.wheels[i]?.querySelector('.wheel-val');
        }
        for (let s = 0; s < CONFIG.STEERING_CHANNELS; s++) {
            this.servoBadges[s] = document.getElementById(`servo-dir-${s}-badge`);
            this.servoLabels[s] = document.getElementById(`servo-${s}-val`);
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
        // Los controles que actuan dependen tambien del mando: lo resuelve
        // renderArmState, que es quien conoce el arbitraje.
        this.renderArmState();
        // Servida desde la ESP no hay nada que desconectar a mano: la pagina
        // llega por el mismo enlace y se reconecta sola.
        if (this.el['btn-disconnect']) {
            this.el['btn-disconnect'].hidden = !connected || servedFromRover();
        }

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
        const control = hasControl();

        this.setBadge('arm-badge', armed ? 'ARMADO' : 'DESARMADO',
                      armed ? 'badge-success' : 'badge-danger');

        const ctrl = this.el['control-badge'];
        if (ctrl) {
            ctrl.hidden = control;
            ctrl.textContent = '👀 OBSERVADOR';
            ctrl.className = 'badge badge-warning';
        }

        const btn = this.el['btn-arm-toggle'];
        if (btn) {
            btn.textContent = armed ? '🛡️ DESARMAR ROVER' : '🛡️ ARMAR ROVER';
            btn.classList.toggle('armed', armed);
        }

        // En observacion se bloquea todo lo que actua. La parada de emergencia
        // no: cualquiera puede detener el rover, tenga el mando o no.
        const connected = State.link.mode !== 'none';
        document.querySelectorAll('[data-needs-control]').forEach((el) => {
            el.disabled = !connected || !control;
        });
    },

    renderTelemetry() {
        const t = State.telemetry;

        this.renderLatency();
        setText(this.el['uptime-val'], formatUptime(t.uptime));
        setText(this.el['heap-val'], `${Math.round(t.heap / 1024)} KB`);
        setText(this.el['clients-val'], String(t.clients));
        setText(this.el['debug-rx-ok'], String(t.rxOk));
        setText(this.el['debug-rx-err'], String(t.rxErr));

        const errEl = this.el['debug-rx-err'];
        if (errEl) errEl.dataset.state = t.rxErr > 0 ? 'warn' : 'ok';

        // Una sola consigna de traccion: el firmware la repite en las 6
        // posiciones de "motors", asi que el esquema anima todas las ruedas.
        const drive = t.motors[0];
        const driveActive = Math.abs(drive) > 1;
        const driveBadge = this.el['pin-drive-badge'];
        if (driveBadge) {
            driveBadge.className = driveActive ? 'pin-badge pin-active' : 'pin-badge pin-idle';
            driveBadge.textContent = driveActive ? `${Math.round(drive)} %` : 'INACTIVO';
        }

        for (let i = 0; i < CONFIG.MOTOR_COUNT; i++) {
            const spd = t.motors[i];
            const active = Math.abs(spd) > 1;

            setText(this.wheelVals[i], `${Math.round(spd)}%`);

            const wheel = this.wheels[i];
            if (wheel) {
                wheel.classList.toggle('active', active);
                wheel.classList.toggle('reverse', spd < -1);
                wheel.style.setProperty('--wheel-power', `${Math.abs(spd)}%`);
            }
        }

        for (let s = 0; s < CONFIG.STEERING_CHANNELS; s++) {
            const ang = t.servos[s];
            const badge = this.servoBadges[s];
            if (badge) {
                badge.textContent = steerLabel(ang);
                badge.className = (ang < 80 || ang > 100)
                    ? 'pin-badge pin-active' : 'pin-badge pin-i2c';
            }
            setText(this.servoLabels[s], steerLabel(ang));
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

    // Enlace + un barrido por servo + una prueba del canal unico de traccion.
    get totalSteps() { return 1 + CONFIG.STEERING_CHANNELS + 1; },

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

        this._render(1, 'Comprobacion del enlace',
            `Enlace: <strong>${State.link.mode.toUpperCase()}</strong> · ` +
            `latencia <strong>${State.link.latency ?? '--'} ms</strong> · ` +
            `tramas con error: <strong>${State.telemetry.rxErr}</strong><br><br>` +
            `Se probaran los ${CONFIG.STEERING_CHANNELS} servos de esquina y el canal ` +
            `de traccion. Aparta las manos del chasis antes de continuar.`);
        if (!await this._await(token)) return false;

        for (let ch = 0; ch < CONFIG.STEERING_CHANNELS; ch++) {
            if (!alive()) return false;
            this._render(2 + ch, `Servo ${SERVO_NAMES[ch]} · canal ${ch}`,
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

        if (!alive()) return false;
        this._render(2 + CONFIG.STEERING_CHANNELS, DRIVE_NAME,
            `Impulso de avance y retroceso al ${CONFIG.WIZARD_SPEED} %. ` +
            '¿Giraron las 6 ruedas en ambos sentidos?');

        this._setBusy(true);
        for (const speed of [CONFIG.WIZARD_SPEED, -CONFIG.WIZARD_SPEED, 0]) {
            if (!alive()) { State.manual = null; return false; }
            if (speed === 0) {
                State.manual = null;
                Link.sendEvent({ action: 'TEST_MOTOR', motor: 0, speed: 0 });
            } else {
                State.manual = { motor: 0, speed };
            }
            await sleep(700);
        }
        this._setBusy(false);
        if (!await this._await(token)) return false;

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

        /*
         * Servida desde la propia ESP: el rover esta al otro extremo de esta
         * misma conexion, asi que no tiene sentido preguntar IP ni ofrecer el
         * cable. Se conecta sola al origen.
         */
        if (servedFromRover()) {
            View.el['login-screen']?.classList.add('web-mode');
            this.connectWifi(location.hostname, location.port || '80');
        }
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

    async connectWifi(hostOverride, portOverride) {
        const ip = hostOverride || View.el['rover-ip'].value.trim() || '192.168.4.1';
        const port = portOverride || View.el['rover-port'].value.trim() || '80';
        View.setLoginStatus(`Conectando con ${ip}:${port}...`, 'info');

        const transport = new WsTransport(
            (line) => Link.handleLine(line),
            () => this.handleTransportClosed());
        try {
            await transport.open(ip, port);
            await this.onConnected(transport, 'ws');
        } catch (err) {
            if (servedFromRover()) {
                // La pagina vino de la ESP, asi que el rover esta ahi: el corte
                // es del Wi-Fi. Reintenta solo en vez de pedir una IP que el
                // operario no tiene forma de saber mejor que el navegador.
                View.setLoginStatus('Reconectando con el rover...', 'info');
                this.scheduleReconnect();
            } else {
                View.setLoginStatus('Sin respuesta. Revisa la IP o conecta por cable.', 'error');
            }
        }
    },

    reconnectTimer: null,

    scheduleReconnect() {
        clearTimeout(this.reconnectTimer);
        this.reconnectTimer = setTimeout(() => {
            this.connectWifi(location.hostname, location.port || '80');
        }, 1500);
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
        clearTimeout(this.reconnectTimer);
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

        if (servedFromRover()) {
            View.setLoginStatus('Enlace perdido. Reconectando...', 'info');
            this.scheduleReconnect();
        } else {
            View.setLoginStatus('Enlace cerrado.', 'error');
        }
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

    /** Corte total: traccion, direccion y armado. Tecla Escape o barra espaciadora. */
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
     * Entra en prueba manual de traccion.
     *
     * Mientras dura, el lazo TX deja de emitir DRIVE y manda TEST_MOTOR. Se
     * desarma primero: una prueba solo tiene sentido con el rover quieto. El
     * campo "motor" se conserva por compatibilidad de protocolo, pero con las
     * 6 ruedas en paralelo el firmware lo ignora.
     */
    beginManualTest(speed) {
        if (State.armed) {
            this.disarm();
            View.flashNotice('Rover desarmado para ejecutar la prueba de traccion.');
        }
        State.manual = { motor: 0, speed: Number(speed) };
    },

    /** Deslizador de traccion del panel de diagnostico. */
    setDrive(value) {
        const speed = Number(value);
        const el = document.querySelector('.motor-control-item .val-display');
        if (el) el.textContent = `${speed}%`;

        if (speed === 0) {
            State.manual = null;
            Link.sendEvent({ action: 'TEST_MOTOR', motor: 0, speed: 0 });
        } else {
            this.beginManualTest(speed);
        }
    },

    stopDrive() {
        State.manual = null;
        document.querySelectorAll('.motor-control-item input[type="range"]').forEach((r) => { r.value = 0; });
        document.querySelectorAll('.motor-control-item .val-display').forEach((d) => { d.textContent = '0%'; });
        Link.sendEvent({ action: 'TEST_MOTOR', motor: 0, speed: 0 });
    },

    // El angulo mostrado NO se fija aqui: llega de vuelta en la telemetria, que
    // es lo que el rover tiene puesto de verdad y ademas mantiene sincronizados
    // a todos los clientes conectados.
    setServo(channel, value) {
        Link.sendEvent({
            action: 'SERVO_SET',
            channel: Number(channel),
            angle: Number(value),
        });
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
                this.beginManualTest(sign * CONFIG.TEST_SPEED);
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

    // Cualquier rueda abre la misma prueba: con los 6 motores en paralelo no
    // hay nada individual que probar. El indice solo sirve para posicionar el
    // popover junto a la rueda pulsada.
    openMotorPopover(index, wheelEl) {
        this.releaseMotorTest();
        this.popoverMotor = index;

        const popover = View.el['motor-test-popover'];
        const container = document.querySelector('.rover-view');
        if (!popover || !container) return;

        View.el['popover-title'].textContent = DRIVE_NAME;

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
        State.manual = null;
        Link.sendEvent({ action: 'TEST_MOTOR', motor: 0, speed: 0 });
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

/** true si este panel puede actuar: o tiene el mando, o esta libre. */
function hasControl() {
    return State.owner === '' || State.owner === CLIENT_ID;
}

/** true si la ESP sirvio esta pagina (en vez de abrirse desde disco con file://). */
function servedFromRover() {
    return location.protocol === 'http:' || location.protocol === 'https:';
}

function formatUptime(seconds) {
    const s = Math.max(0, Math.floor(seconds));
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    const sec = s % 60;
    if (h > 0) return `${h}h ${String(m).padStart(2, '0')}m`;
    if (m > 0) return `${m}m ${String(sec).padStart(2, '0')}s`;
    return `${sec}s`;
}

document.addEventListener('DOMContentLoaded', () => App.init());
