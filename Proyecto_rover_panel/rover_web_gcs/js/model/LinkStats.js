'use strict';

/**
 * Historial y analisis del enlace para el apartado "Señal".
 *
 * Guarda una muestra cada SAMPLE_MS (calidad, latencia, retransmisiones,
 * cadencia de telemetria...) y un registro de eventos: cortes, reconexiones,
 * reinicios del rover o de la base y fallos del modulo de radio. Con eso
 * propone un diagnostico de por que se desconecta.
 *
 * Eventos: 'sample' (nueva muestra), 'event' (nuevo evento), 'reset'.
 */
class LinkStats extends Emitter {
    constructor(model) {
        super();
        this.model = model;
        this.active = false;
        this.reset();

        model.on('link', () => this._onLink());
        model.on('telemetry', () => this._onTelemetry(performance.now()));
        model.on('base', () => this._onBase());
        model.on('reconnect', ({ prev, state }) => this._onReconnect(prev, state, performance.now()));
        model.on('link-giveup', ({ cause }) => this._event('danger', 'Desconectado',
            cause === 'usb' ? 'El USB no volvió tras los intentos.' : 'La radio no volvió tras los intentos.'));
        model.on('link-usb-restored', ({ attempt }) => this._event('ok', 'USB recuperado', `En el intento ${attempt}.`));

        setInterval(() => this._sample(performance.now()), CONFIG.SAMPLE_MS);
    }

    reset() {
        this.samples = [];
        this.events = [];
        this.sessionStart = performance.now();
        this.drops = 0;
        this.recovered = 0;
        this.longestDropMs = 0;
        this.roverResets = 0;
        this.baseResets = 0;
        this.chipFailures = 0;
        this.outage = null;                      // { start, attempts }
        this.lastTelAt = 0;
        this.telCount = 0;
        this.maxGap = 0;
        this.prevRoverUptime = null;
        this.prevBase = null;
        this.reinitAtStart = null;
        this.emit('reset');
    }

    /* --- Entradas --- */

    _onLink() {
        const connected = this.model.connected;
        if (connected && !this.active) {
            this.active = true;
            this.reset();
            this._event('info', 'Sesión iniciada', this.model.link.mode === 'demo' ? 'Simulación.' : 'USB.');
        } else if (!connected && this.active) {
            this.active = false;
            if (this.outage) this._closeOutage(performance.now(), false);
            this._event('info', 'Sesión cerrada', '');
        }
    }

    _onTelemetry(now) {
        if (this.lastTelAt) this.maxGap = Math.max(this.maxGap, now - this.lastTelAt);
        this.lastTelAt = now;
        this.telCount++;

        // Si el uptime del rover retrocede, el rover se reinicio: casi siempre
        // es un bajon de tension al arrancar motores o servos.
        const up = this.model.telemetry.uptime;
        if (this.prevRoverUptime !== null && up + 1 < this.prevRoverUptime) {
            this.roverResets++;
            this._event('danger', 'El rover se reinició',
                `Llevaba ${Format.uptime(this.prevRoverUptime)} encendido. Suele ser una caída de tensión.`);
        }
        this.prevRoverUptime = up;
    }

    _onBase() {
        const b = this.model.radio;
        const prev = this.prevBase;
        if (this.reinitAtStart === null && b.reinit !== null) this.reinitAtStart = b.reinit;

        if (prev) {
            if (b.up !== null && prev.up !== null && b.up + 1 < prev.up) {
                this.baseResets++;
                this._event('danger', 'La estación base se reinició',
                    'Revisa el cable USB y el condensador del nRF24 de la base.');
            }
            if (b.chip === false && prev.chip !== false) {
                this.chipFailures++;
                this._event('danger', 'El nRF24 de la base no responde', 'Fallo por SPI: cableado o 3,3 V de la base.');
            }
            if (b.reinit !== null && prev.reinit !== null && b.reinit > prev.reinit) {
                this._event('warn', 'La base reconfiguró su radio', `Intento ${b.retry ?? '?'} de la base.`);
            }
        }
        this.prevBase = { up: b.up, chip: b.chip, reinit: b.reinit };
    }

    _onReconnect(prev, state, now) {
        if (!prev && state) {
            this.drops++;
            this.outage = { start: state.since ?? now, attempts: state.attempt, cause: state.cause };
            this._event('warn', state.cause === 'usb' ? 'Corte del USB' : 'Corte de radio',
                'Tracción cortada por el rover. Reintentando sin desarmar.');
        } else if (prev && state) {
            if (this.outage) this.outage.attempts = state.attempt;
            this._event('warn', `Intento ${state.attempt} de ${CONFIG.RETRY_COUNT}`, '');
        } else if (prev && !state) {
            this._closeOutage(now, this.model.connected);
        }
    }

    _closeOutage(now, ok) {
        const o = this.outage;
        if (!o) return;
        const dur = now - o.start;
        this.longestDropMs = Math.max(this.longestDropMs, dur);
        if (ok) {
            this.recovered++;
            this._event('ok', 'Enlace recuperado', `Tras ${(dur / 1000).toFixed(1)} s, en el intento ${o.attempts}.`);
        }
        this.outage = null;
    }

    _event(level, title, text) {
        const e = { at: new Date(), level, title, text };
        this.events.unshift(e);
        if (this.events.length > 200) this.events.pop();
        this.emit('event', e);
    }

    /* --- Muestreo --- */

    _sample(now) {
        if (!this.active) return;
        const m = this.model;
        const b = m.radio;
        const windowS = CONFIG.SAMPLE_MS / 1000;

        const s = {
            t: now,
            lq: b ? b.lq : (m.link.stale ? 0 : 100),
            latency: m.link.latency,
            arc: b ? b.arc : null,
            rate: this.telCount / windowS,                       // telemetria por segundo
            gap: this.lastTelAt ? Math.max(this.maxGap, now - this.lastTelAt) : null,
            lost: Boolean(m.reconnect) || m.link.stale,
            txOk: b ? b.txOk : null,
            txFail: b ? b.txFail : null,
        };
        this.telCount = 0;
        this.maxGap = 0;

        this.samples.push(s);
        const max = Math.round(CONFIG.HISTORY_S * 1000 / CONFIG.SAMPLE_MS);
        if (this.samples.length > max) this.samples.shift();
        this.emit('sample', s);
    }

    /* --- Consultas --- */

    get last() { return this.samples[this.samples.length - 1] || null; }

    /** Media de un campo en los ultimos n segundos (ignora null). */
    avg(field, seconds = 10) {
        const from = performance.now() - seconds * 1000;
        const vals = this.samples.filter((s) => s.t >= from && s[field] !== null).map((s) => s[field]);
        return vals.length ? vals.reduce((a, b) => a + b, 0) / vals.length : null;
    }

    lossPercent() {
        const b = this.model.radio;
        if (!b || b.txOk + b.txFail === 0) return null;
        return (100 * b.txFail) / (b.txOk + b.txFail);
    }

    /**
     * Diagnostico en lenguaje llano. Cada hallazgo: { level, title, text }.
     * Orden: lo mas grave primero.
     */
    diagnose() {
        const out = [];
        const m = this.model;
        const b = m.radio;
        const lq = this.avg('lq', 10);
        const arc = this.avg('arc', 10);
        const lat = this.avg('latency', 10);

        if (!m.connected) return [{ level: 'info', title: 'Sin conexión', text: 'Conecta la estación base para analizar la señal.' }];

        if (this.roverResets > 0) {
            out.push({ level: 'danger', title: `El rover se reinició ${this.roverResets} ${this.roverResets === 1 ? 'vez' : 'veces'}`,
                text: 'Es la causa más común de "se desconecta": al arrancar motores o servos cae la tensión y la ESP32 se reinicia. Revisa la fuente, separa la alimentación de la lógica y pon condensadores.' });
        }
        if (b && b.chip === false) {
            out.push({ level: 'danger', title: 'El nRF24 de la base no responde',
                text: 'La base no habla con su módulo por SPI. Revisa los cables CE, CSN, SCK, MOSI, MISO, 3,3 V y GND de la base.' });
        } else if (this.chipFailures > 0) {
            out.push({ level: 'warn', title: 'El nRF24 de la base falló durante la sesión',
                text: 'Dejó de responder por SPI en algún momento: suele ser un cable flojo o falta de condensador.' });
        }
        if (this.baseResets > 0) {
            out.push({ level: 'danger', title: 'La estación base se reinició',
                text: 'Revisa el cable y el puerto USB, y que el nRF24 de la base tenga su condensador de 10–100 µF.' });
        }
        if (b && !b.rover && !this.roverResets) {
            out.push({ level: 'warn', title: 'La base no recibe respuesta del rover',
                text: 'La base funciona, pero el rover no contesta. Mira si el rover está encendido, su nRF24 (CE suelto o sin condensador) y la distancia.' });
        }
        if (this.drops > 0) {
            const sudden = this._suddenDrops();
            out.push({ level: 'warn',
                title: `${this.drops} ${this.drops === 1 ? 'corte' : 'cortes'} en la sesión (el más largo, ${(this.longestDropMs / 1000).toFixed(1)} s)`,
                text: sudden
                    ? 'Los cortes llegan de golpe, sin que la señal empeore antes: apunta a alimentación o a un cable que se suelta con las vibraciones, no a la distancia.'
                    : 'Antes de cortarse la señal se degradaba: apunta a distancia, antena u obstáculos.' });
        }
        if (arc !== null && arc >= 2) {
            out.push({ level: 'warn', title: `Muchas retransmisiones (${arc.toFixed(1)} por paquete)`,
                text: 'Cada paquete necesita varios intentos. Es la primera señal de un enlace al límite: acerca las antenas, quita metal del medio, sube la potencia (kPaLevel) o cambia de canal si hay WiFi cerca.' });
        }
        if (lq !== null && lq < 70) {
            out.push({ level: 'warn', title: `Calidad baja (${Math.round(lq)} %)`,
                text: 'Se pierde uno de cada tres paquetes o más. Con más pérdidas saltará la reconexión.' });
        }
        if (lat !== null && lat > 150) {
            out.push({ level: 'warn', title: `Latencia alta (${Math.round(lat)} ms)`,
                text: 'Lo normal por radio son 20–60 ms. Puede ser el PC (otra pestaña pesada) o muchos reintentos de radio.' });
        }
        if (b && b.arc === null) {
            out.push({ level: 'info', title: 'Faltan datos de la base',
                text: 'La estación base tiene el firmware anterior: súbele el nuevo para ver retransmisiones, estado del chip y reinicios.' });
        }
        if (!b && m.link.mode === 'serial') {
            out.push({ level: 'info', title: 'Sin estación base',
                text: 'El panel está enchufado directo al rover (o la base no informa). Sin radio no hay señal que analizar.' });
        }
        if (out.length === 0) {
            out.push({ level: 'ok', title: 'Enlace sano', text: 'Sin cortes, reinicios ni degradación en esta sesión.' });
        }
        return out;
    }

    /** true si en los cortes la calidad estaba alta justo antes (corte brusco). */
    _suddenDrops() {
        let sudden = 0, total = 0;
        for (let i = 1; i < this.samples.length; i++) {
            if (this.samples[i].lost && !this.samples[i - 1].lost) {
                total++;
                const before = this.samples.slice(Math.max(0, i - 6), i);
                const avgLq = before.reduce((a, s) => a + s.lq, 0) / (before.length || 1);
                if (avgLq >= 90) sudden++;
            }
        }
        return total > 0 && sudden / total >= 0.5;
    }

    /** Muestras y eventos en CSV, para analizar los cortes despues. */
    toCsv() {
        const t0 = this.sessionStart;
        const rows = ['tipo;segundo;calidad_%;latencia_ms;retransmisiones;telemetria_hz;hueco_ms;perdido;detalle'];
        for (const s of this.samples) {
            rows.push(['muestra', ((s.t - t0) / 1000).toFixed(1), s.lq, s.latency ?? '', s.arc ?? '',
                       s.rate.toFixed(1), s.gap === null ? '' : Math.round(s.gap), s.lost ? 1 : 0, ''].join(';'));
        }
        for (const e of [...this.events].reverse()) {
            rows.push(['evento', e.at.toLocaleTimeString(), '', '', '', '', '', '', `${e.title}. ${e.text}`.replace(/;/g, ',')].join(';'));
        }
        return rows.join('\n');
    }
}
