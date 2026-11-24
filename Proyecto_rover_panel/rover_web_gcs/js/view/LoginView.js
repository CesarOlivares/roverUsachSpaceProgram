'use strict';

/**
 * Pantalla de conexion.
 * Intenciones: 'serial', 'wifi' (host), 'demo'.
 */
class LoginView extends Emitter {
    constructor() {
        super();
        this.screen = Dom.byId('login-screen');
        this.status = Dom.byId('login-status');
        this.hostInput = Dom.byId('wifi-host');
        this.roverLinks = Dom.byId('wifi-rover-links');

        Dom.byId('btn-connect-serial')?.addEventListener('click', () => this.emit('serial'));
        Dom.byId('btn-connect-demo')?.addEventListener('click', () => this.emit('demo'));
        Dom.byId('btn-connect-wifi')?.addEventListener('click', () => this.emit('wifi', this.hostInput?.value));
        this.hostInput?.addEventListener('keydown', (ev) => {
            if (ev.key === 'Enter') this.emit('wifi', this.hostInput.value);
        });
        this.roverLinkList = Dom.byId('wifi-rover-link-list');

        if (this.hostInput) this.hostInput.value = this._initialHost();
        // En GitHub Pages (https) el Wi-Fi solo va abriendo el panel del propio rover.
        if (!WsTransport.allowedHere) this.showRoverLink(this.hostInput?.value || CONFIG.WIFI_DEFAULT_HOST);
    }

    /**
     * Si el panel lo sirve el propio rover (http://<ip>/), esa es su direccion.
     * Si no, la ultima usada o el punto de acceso del rover.
     */
    _initialHost() {
        const servedByRover = location.protocol === 'http:' && location.host &&
            !/^(localhost|127\.0\.0\.1)(:\d+)?$/.test(location.host);
        if (servedByRover) return location.host;
        try { return localStorage.getItem('rover.wifiHost') || CONFIG.WIFI_DEFAULT_HOST; } catch (e) { return CONFIG.WIFI_DEFAULT_HOST; }
    }

    rememberHost(host) {
        try { localStorage.setItem('rover.wifiHost', host); } catch (e) { /* sin almacenamiento */ }
    }

    /** Enlaces al panel servido por el rover (la unica forma de usar Wi-Fi desde https). */
    showRoverLink(host) {
        if (!this.roverLinks || !this.roverLinkList) return;
        const hosts = [...new Set([host, CONFIG.WIFI_DEFAULT_HOST, CONFIG.WIFI_MDNS_HOST].filter(Boolean))];
        this.roverLinkList.replaceChildren(...hosts.map((h) => {
            const a = document.createElement('a');
            a.href = `http://${h}/`;
            a.textContent = `http://${h}/`;
            a.className = 'rover-link';
            return a;
        }));
        this.roverLinks.hidden = false;
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
