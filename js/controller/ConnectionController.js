'use strict';

/**
 * Abre y cierra el enlace: USB con la estacion base (o directo al rover para
 * pruebas en banco) o el simulador.
 *
 * Si el USB se cae solo (cable flojo, base reiniciada), no se vuelve a la
 * pantalla de conexion de inmediato: se intenta reabrir el mismo puerto
 * RETRY_COUNT veces. Mientras, el firmware del rover ya corto la traccion.
 */
class ConnectionController {
    constructor(model, link, loginView, { drive, preflight, safety }) {
        this.model = model;
        this.link = link;
        this.login = loginView;
        this.drive = drive;
        this.preflight = preflight;
        this.safety = safety;
        this.retryToken = 0;

        link.on('telemetry', ({ data, line }) => {
            model.noteWire('rx', line);
            model.applyTelemetry(data, performance.now());
        });
        link.on('base', (status) => model.applyBaseStatus(status, performance.now()));
        link.on('tx', (payload) => model.noteWire('tx', payload));
        link.on('failed', () => this._onTransportClosed(this.link.transport));
    }

    async connectSerial() {
        this.login.setStatus('Solicitando puerto serie...', 'info');
        const transport = new SerialTransport(
            (line) => this.link.handleLine(line),
            () => this._onTransportClosed(transport));
        try {
            await transport.open();
            await this._onConnected(transport, 'serial');
        } catch (err) {
            this.login.setStatus(err.message || 'No se pudo abrir el puerto serie.', 'error');
        }
    }

    async connectDemo() {
        const transport = new DemoTransport((line) => this.link.handleLine(line));
        await transport.open();
        await this._onConnected(transport, 'demo');
    }

    /**
     * Cierre pedido (boton o reconexion agotada). La parada sale directa, sin
     * cola: con sendEvent se quedaba en la cola que close() vacia y no llegaba.
     */
    async disconnect(reason) {
        this.retryToken++;                       // cancela una reconexion USB en curso
        this.preflight.abort();
        this.model.requestDisarm(performance.now());
        this.model.releaseKeys();
        this.model.emit('estop');
        this.drive.stop();
        await this.link.sendNow(Protocol.stop());
        await this.link.close();
        this.handleClosed(reason);
    }

    /** El transporte se cayo o se cerro: vuelta a la pantalla de conexion. */
    handleClosed(reason) {
        if (!this.model.connected) return;

        this.drive.stop();
        this.preflight.abort();
        this.link.detach();
        this.model.endSession();
        this.login.show(true);
        this.login.setStatus(reason || 'Enlace cerrado. Si se desenchufó la estación base, vuelve a conectarla.', 'error');
    }

    async _onConnected(transport, mode) {
        // Cierra cualquier enlace previo: si no, su lector y su temporizador
        // seguirian vivos y habria dos pipelines escribiendo a la vez.
        this.retryToken++;
        this.drive.stop();
        this.preflight.abort();
        await this.link.close();

        this.link.attach(transport);
        this.model.beginSession(mode);

        this.login.show(false);
        this.login.setStatus('', '');

        document.activeElement?.blur();          // que WASD llegue a window
        window.focus();

        this.drive.start();
    }

    // Solo cuenta el cierre del transporte activo: el de una sesion anterior
    // que se cierra con retraso no debe tumbar la actual.
    _onTransportClosed(transport) {
        if (!transport || this.link.transport !== transport) return;
        if (this.model.link.mode === 'serial' && transport.info) {
            this._retryUsb(transport);
        } else {
            this.handleClosed();
        }
    }

    async _retryUsb(old) {
        const token = ++this.retryToken;
        const m = this.model;
        const info = old.info;
        this.link.detach();
        // Si fallo la escritura pero el puerto sigue abierto, hay que cerrarlo
        // o no se podra reabrir.
        old.close().catch(() => { /* ya cerrado */ });
        m.emit('notice', 'Se cortó el USB con la estación base. Reintentando...');

        for (let attempt = 1; attempt <= CONFIG.RETRY_COUNT; attempt++) {
            m.setReconnect({ attempt, cause: 'usb', since: performance.now() });
            await sleep(CONFIG.RETRY_WINDOW_MS);
            if (token !== this.retryToken) return;   // el operador desconecto

            try {
                const transport = await SerialTransport.reopen(
                    info,
                    (line) => this.link.handleLine(line),
                    () => this._onTransportClosed(transport));
                if (token !== this.retryToken) { await transport.close(); return; }

                this.link.attach(transport);
                // Ventana limpia para la vigilancia de radio: el silencio del USB
                // no cuenta como intentos de radio.
                m.lastRxAt = performance.now();
                m.setReconnect(null);
                m.emit('link-usb-restored', { attempt });
                m.emit('notice', `USB recuperado en el intento ${attempt}.`);
                return;
            } catch (err) {
                console.debug(`[usb] intento ${attempt} fallido:`, err.message);
            }
        }

        if (token !== this.retryToken) return;
        m.emit('link-giveup', { cause: 'usb' });
        m.setReconnect(null);
        this.handleClosed(`Se desconectó la estación base (USB) y no volvió tras ${CONFIG.RETRY_COUNT} intentos. ` +
                          'El rover cortó la tracción y se desarmó.');
    }
}
