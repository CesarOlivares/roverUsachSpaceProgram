'use strict';

/**
 * Cabecera: estado del enlace, latencia, armado, observador, pestanas y avisos.
 * Intenciones: 'disconnect'.
 */
class HeaderView extends Emitter {
    constructor(model) {
        super();
        this.model = model;

        this.status = Dom.byId('status-badge');
        this.latency = Dom.byId('latency-badge');
        this.arm = Dom.byId('arm-badge');
        this.control = Dom.byId('control-badge');
        this.disconnectBtn = Dom.byId('btn-disconnect');
        this.notice = Dom.byId('notice-bar');
        this.noticeTimer = null;

        this.tabs = {
            general: { tab: Dom.byId('tab-general'), view: Dom.byId('view-general') },
            diagnostics: { tab: Dom.byId('tab-diagnostics'), view: Dom.byId('view-diagnostics') },
        };
        for (const [mode, { tab }] of Object.entries(this.tabs)) {
            tab?.addEventListener('click', () => this.switchTab(mode));
        }

        this.disconnectBtn?.addEventListener('click', () => this.emit('disconnect'));

        model.on('link', () => { this.renderLink(); this.renderLatency(); });
        model.on('telemetry', () => this.renderLatency());
        model.on('arm', () => this.renderArm());
        model.on('control', () => this.renderArm());
        model.on('notice', (msg) => this.flashNotice(msg));

        this.renderLink();
        this.renderLatency();
        this.renderArm();
    }

    switchTab(mode) {
        for (const [name, { tab, view }] of Object.entries(this.tabs)) {
            tab?.classList.toggle('active', name === mode);
            view?.classList.toggle('hidden', name !== mode);
        }
    }

    renderLink() {
        const { mode, stale } = this.model.link;

        if (mode === 'none') {
            Dom.setBadge(this.status, Format.linkLabel('none'), 'badge-danger');
        } else if (stale) {
            Dom.setBadge(this.status, `${Format.linkLabel(mode)} · SIN DATOS`, 'badge-warning');
        } else {
            Dom.setBadge(this.status, Format.linkLabel(mode), 'badge-success');
        }

        // Servida desde la ESP no hay nada que desconectar a mano: la pagina
        // llega por el mismo enlace y se reconecta sola.
        if (this.disconnectBtn) {
            this.disconnectBtn.hidden = mode === 'none' || servedFromRover();
        }
    }

    renderLatency() {
        const { mode, stale, latency } = this.model.link;
        if (mode === 'none' || stale || latency === null) {
            Dom.setBadge(this.latency, 'Latencia: --', 'badge-gray');
            return;
        }
        Dom.setBadge(this.latency, `Latencia: ${latency} ms`,
                     latency > 150 ? 'badge-warning' : 'badge-gray');
    }

    renderArm() {
        const m = this.model;
        if (m.armPending?.target) {
            Dom.setBadge(this.arm, 'ARMANDO…', 'badge-warning');
        } else {
            Dom.setBadge(this.arm, m.armed ? 'ARMADO' : 'DESARMADO',
                         m.armed ? 'badge-success' : 'badge-danger');
        }

        if (this.control) {
            this.control.hidden = m.hasControl();
            this.control.textContent = '👀 OBSERVADOR';
            this.control.className = 'badge badge-warning';
        }
    }

    flashNotice(message) {
        if (!this.notice) return;
        this.notice.textContent = message;
        this.notice.classList.add('visible');
        clearTimeout(this.noticeTimer);
        this.noticeTimer = setTimeout(() => this.notice.classList.remove('visible'), 4000);
    }
}
