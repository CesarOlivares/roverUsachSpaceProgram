'use strict';

/**
 * Columna de estado y seguridad: metricas, entradas activas y botones de armado.
 * Tambien bloquea todo lo marcado con data-needs-control cuando este panel no
 * puede actuar.
 * Intenciones: 'toggle-arm', 'preflight', 'estop'.
 */
class OperationView extends Emitter {
    constructor(model) {
        super();
        this.model = model;

        this.uptime = Dom.byId('uptime-val');
        this.heap = Dom.byId('heap-val');
        this.clients = Dom.byId('clients-val');
        this.chipWasd = Dom.byId('hud-wasd');
        this.chipGamepad = Dom.byId('hud-gamepad');
        this.armBtn = Dom.byId('btn-arm-toggle');

        this.armBtn?.addEventListener('click', () => this.emit('toggle-arm'));
        Dom.byId('btn-preflight')?.addEventListener('click', () => this.emit('preflight'));
        Dom.byId('btn-estop')?.addEventListener('click', () => this.emit('estop'));

        model.on('telemetry', () => this.renderMetrics());
        model.on('inputs', () => this.renderInputs());
        model.on('arm', () => this.renderControls());
        model.on('control', () => this.renderControls());
        model.on('link', () => this.renderControls());

        this.renderInputs();
        this.renderControls();
    }

    renderMetrics() {
        const t = this.model.telemetry;
        Dom.setText(this.uptime, Format.uptime(t.uptime));
        Dom.setText(this.heap, `${Math.round(t.heap / 1024)} KB`);
        Dom.setText(this.clients, String(t.clients));
    }

    renderInputs() {
        this.chipWasd?.classList.toggle('active', this.model.keyboardActive);
        this.chipGamepad?.classList.toggle('active', this.model.gamepad);
    }

    renderControls() {
        const m = this.model;
        const armedOrArming = m.armed || Boolean(m.armPending?.target);

        if (this.armBtn) {
            this.armBtn.textContent = armedOrArming ? '🛡️ DESARMAR ROVER' : '🛡️ ARMAR ROVER';
            this.armBtn.classList.toggle('armed', armedOrArming);
        }

        // En observacion se bloquea todo lo que actua. La parada de emergencia
        // no: cualquiera puede detener el rover, tenga el mando o no.
        const enabled = m.connected && m.hasControl();
        document.querySelectorAll('[data-needs-control]').forEach((el) => {
            el.disabled = !enabled;
        });
    }
}
