'use strict';

/**
 * Pantalla de conexion.
 * Intenciones: 'serial', 'demo'.
 */
class LoginView extends Emitter {
    constructor() {
        super();
        this.screen = Dom.byId('login-screen');
        this.status = Dom.byId('login-status');

        Dom.byId('btn-connect-serial')?.addEventListener('click', () => this.emit('serial'));
        Dom.byId('btn-connect-demo')?.addEventListener('click', () => this.emit('demo'));
    }

    show(visible) {
        if (this.screen) this.screen.style.display = visible ? 'flex' : 'none';
    }

    setStatus(text, kind) {
        if (!this.status) return;
        this.status.textContent = text;
        this.status.dataset.kind = kind || '';
    }
}
