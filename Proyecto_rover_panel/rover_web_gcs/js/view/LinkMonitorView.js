'use strict';

/** Monitor de enlace: estado, contadores y ultima trama en cada sentido. Solo lectura. */
class LinkMonitorView {
    constructor(model) {
        this.model = model;

        this.status = Dom.byId('debug-link-status');
        this.rxOk = Dom.byId('debug-rx-ok');
        this.rxErr = Dom.byId('debug-rx-err');
        this.tx = Dom.byId('debug-tx-data');
        this.rx = Dom.byId('debug-rx-data');

        model.on('link', () => this.renderLink());
        model.on('telemetry', () => this.renderCounters());
        model.on('wire', ({ dir, text }) => Dom.setText(dir === 'tx' ? this.tx : this.rx, text));

        this.renderLink();
    }

    renderLink() {
        if (!this.status) return;
        const { mode, stale } = this.model.link;
        const label = Format.linkLabel(mode);
        this.status.textContent = mode === 'none' ? 'Desconectado'
                                : stale ? `${label} (sin telemetria)`
                                : label;
        this.status.dataset.state = mode === 'none' ? 'off' : stale ? 'warn' : 'ok';
    }

    renderCounters() {
        const t = this.model.telemetry;
        Dom.setText(this.rxOk, String(t.rxOk));
        Dom.setText(this.rxErr, String(t.rxErr));
        if (this.rxErr) this.rxErr.dataset.state = t.rxErr > 0 ? 'warn' : 'ok';
    }
}
