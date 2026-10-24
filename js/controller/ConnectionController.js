'use strict';

/** Abre, cierra y recupera el enlace con el rover por cualquiera de los transportes. */
class ConnectionController {
    constructor(model, link, loginView, { drive, preflight, safety }) {
        this.model = model;
        this.link = link;
        this.login = loginView;
        this.drive = drive;
        this.preflight = preflight;
        this.safety = safety;
        this.reconnectTimer = null;

        link.on('telemetry', ({ data, line }) => {
            model.noteWire('rx', line);
            model.applyTelemetry(data, performance.now());
        });
        link.on('tx', (payload) => model.noteWire('tx', payload));
        link.on('failed', () => this.handleClosed());
    }

    /** Servida desde la propia ESP, el rover esta al otro extremo: se conecta sola. */
    autoConnect() {
        if (!servedFromRover()) return;
        this.login.setWebMode();
        this.connectWifi(location.hostname, location.port || '80');
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

    async connectWifi(ip, port) {
        this.login.setStatus(`Conectando con ${ip}:${port}...`, 'info');

        const transport = new WsTransport(
            (line) => this.link.handleLine(line),
            () => this._onTransportClosed(transport));
        try {
            await transport.open(ip, port);
            await this._onConnected(transport, 'ws');
        } catch (err) {
            if (servedFromRover()) {
                // La pagina vino de la ESP, asi que el rover esta ahi: el corte
                // es del Wi-Fi. Reintenta solo en vez de pedir una IP.
                this.login.setStatus('Reconectando con el rover...', 'info');
                this._scheduleReconnect();
            } else {
                this.login.setStatus('Sin respuesta. Revisa la IP o conecta por cable.', 'error');
            }
        }
    }

    async connectDemo() {
        const transport = new DemoTransport((line) => this.link.handleLine(line));
        await transport.open();
        await this._onConnected(transport, 'demo');
    }

    async disconnect() {
        clearTimeout(this.reconnectTimer);
        this.preflight.abort();
        this.safety.emergencyStop();
        this.drive.stop();
        await this.link.close();
        this.handleClosed();
    }

    /** El transporte se cayo o se cerro: vuelta a la pantalla de conexion. */
    handleClosed() {
        if (!this.model.connected) return;

        this.drive.stop();
        this.preflight.abort();
        this.link.detach();
        this.model.endSession();
        this.login.show(true);

        if (servedFromRover()) {
            this.login.setStatus('Enlace perdido. Reconectando...', 'info');
            this._scheduleReconnect();
        } else {
            this.login.setStatus('Enlace cerrado.', 'error');
        }
    }

    async _onConnected(transport, mode) {
        // Cierra cualquier enlace previo: si no, su lector y su temporizador
        // seguirian vivos y habria dos pipelines escribiendo a la vez.
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
        if (this.link.transport !== transport) return;
        this.handleClosed();
    }

    _scheduleReconnect() {
        clearTimeout(this.reconnectTimer);
        this.reconnectTimer = setTimeout(() => {
            if (this.model.connected) return;   // ya se reconecto por otro camino
            this.connectWifi(location.hostname, location.port || '80');
        }, CONFIG.RECONNECT_MS);
    }
}
