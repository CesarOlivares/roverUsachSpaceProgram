'use strict';

/**
 * Pantalla de conexion.
 * Intenciones: 'serial', 'wifi' { ip, port }, 'demo'.
 */
class LoginView extends Emitter {
    constructor() {
        super();
        this.screen = Dom.byId('login-screen');
        this.status = Dom.byId('login-status');
        this.ip = Dom.byId('rover-ip');
        this.port = Dom.byId('rover-port');

        Dom.byId('btn-connect-serial')?.addEventListener('click', () => this.emit('serial'));
        Dom.byId('btn-connect-demo')?.addEventListener('click', () => this.emit('demo'));
        Dom.byId('btn-connect-wifi')?.addEventListener('click', () => this.emit('wifi', {
            ip: this.ip?.value.trim() || '192.168.4.1',
            port: this.port?.value.trim() || '80',
        }));
    }

    show(visible) {
        if (this.screen) this.screen.style.display = visible ? 'flex' : 'none';
    }

    /** Servida desde la ESP: se ocultan las opciones, la conexion es automatica. */
    setWebMode() {
        this.screen?.classList.add('web-mode');
    }

    setStatus(text, kind) {
        if (!this.status) return;
        this.status.textContent = text;
        this.status.dataset.kind = kind || '';
    }
}
