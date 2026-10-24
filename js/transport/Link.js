'use strict';

/**
 * Cola de salida unica y recepcion de lineas sobre el transporte activo.
 *
 * Eventos:
 *   tx         payload que acaba de salir (texto)
 *   telemetry  { data, line } telemetria decodificada
 *   failed     el transporte fallo al escribir
 */
class Link extends Emitter {
    constructor() {
        super();
        this.transport = null;

        /*
         * Dos carriles de salida con un unico escritor en vuelo:
         *
         *   events  ARM, DISARM, STEPPER_JOG... no se pueden perder -> cola FIFO
         *   frame   DRIVE, TEST_MOTOR, HEARTBEAT: son instantaneas de estado, si
         *           una se retrasa lo correcto es sustituirla, no encolarla
         *
         * Con dos escritores sin await, cuando el puerto aplicaba contrapresion
         * las promesas se acumulaban y los comandos llegaban cada vez mas tarde.
         */
        this.events = [];
        this.frame = null;
        this.writing = false;
    }

    get connected() { return this.transport !== null; }

    /** Instala un transporte ya abierto. Vacia las colas: nada de una sesion anterior sale en esta. */
    attach(transport) {
        this.transport = transport;
        this.events = [];
        this.frame = null;
        this.writing = false;
    }

    /** Suelta el transporte sin cerrarlo (ya se cerro solo). */
    detach() {
        this.transport = null;
        this.events = [];
        this.frame = null;
    }

    async close() {
        const t = this.transport;
        this.detach();
        if (t) await t.close();
    }

    sendEvent(obj) {
        if (!this.transport) return;
        if (this.events.length >= CONFIG.MAX_QUEUED_EVENTS) this.events.shift();
        this.events.push({ ...obj, client: CLIENT_ID });
        this._pump();
    }

    sendFrame(obj) {
        if (!this.transport) return;
        this.frame = { ...obj, client: CLIENT_ID };
        this._pump();
    }

    /**
     * Envio inmediato saltandose la cola, para la parada al cerrar la pestana:
     * no hay tiempo de esperar a que termine la escritura en curso.
     */
    sendNow(obj) {
        if (!this.transport) return;
        const payload = Protocol.encode({ ...obj, client: CLIENT_ID }) + '\n';
        this.transport.send(payload).catch(() => { /* la pagina se esta cerrando */ });
    }

    async _pump() {
        if (this.writing || !this.transport) return;
        if (this.events.length === 0 && !this.frame) return;

        const batch = this.events;
        this.events = [];
        if (this.frame) { batch.push(this.frame); this.frame = null; }

        const payload = batch.map((o) => Protocol.encode(o)).join('\n') + '\n';
        this.emit('tx', payload.trim());

        this.writing = true;
        try {
            await this.transport.send(payload);
        } catch (err) {
            console.error('Fallo de transmision:', err);
            this.emit('failed');
            return;
        } finally {
            this.writing = false;
        }

        // Mientras se escribia pudo llegar mas trabajo.
        if (this.events.length > 0 || this.frame) this._pump();
    }

    handleLine(rawLine) {
        const line = rawLine.trim();
        if (!line) return;

        const data = Protocol.decodeTelemetry(line);
        if (!data) {
            if (line[0] !== '{') console.debug('[rover]', line);
            return;
        }
        this.emit('telemetry', { data, line });
    }
}
