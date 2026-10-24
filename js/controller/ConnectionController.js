'use strict';

/**
 * Abre y cierra el enlace: USB con la estacion base (o directo al rover para
 * pruebas en banco) o el simulador.
 */
class ConnectionController {
    constructor(model, link, loginView, { drive, preflight, safety }) {
        this.model = model;
        this.link = link;
        this.login = loginView;
        this.drive = drive;
        this.preflight = preflight;
        this.safety = safety;

        link.on('telemetry', ({ data, line }) => {
            model.noteWire('rx', line);
            model.applyTelemetry(data, performance.now());
        });
        link.on('base', (status) => model.applyBaseStatus(status, performance.now()));
        link.on('tx', (payload) => model.noteWire('tx', payload));
        link.on('failed', () => this.handleClosed());
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

    async disconnect() {
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
        this.login.setStatus('Enlace cerrado. Si se desenchufó la estación base, vuelve a conectarla.', 'error');
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
}
