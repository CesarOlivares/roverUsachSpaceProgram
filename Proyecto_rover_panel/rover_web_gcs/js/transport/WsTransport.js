'use strict';

/**
 * Wi-Fi: WebSocket contra el servidor del rover (enlace de emergencia).
 *
 * Desde una pagina https (GitHub Pages) el navegador bloquea ws:// sin cifrar:
 * por eso el rover sirve su propia copia del panel en http://<ip>/.
 */
class WsTransport {
    constructor(onLine, onClose) {
        this.onLine = onLine;
        this.onClose = onClose;
        this.socket = null;
        this.host = null;
        this.closing = false;
    }

    /** true si esta pagina puede abrir ws:// (no esta servida por https). */
    static get allowedHere() { return location.protocol !== 'https:'; }

    /** Normaliza lo que escribe el operador: "http://10.0.0.5/" -> "10.0.0.5". */
    static cleanHost(text) {
        return String(text || '').trim()
            .replace(/^[a-z]+:\/\//i, '')
            .replace(/\/.*$/, '');
    }

    open(host, timeoutMs = CONFIG.WIFI_OPEN_TIMEOUT_MS) {
        this.host = host;
        const url = `ws://${host}/ws`;
        return new Promise((resolve, reject) => {
            let settled = false;
            const fail = (msg) => {
                if (settled) return;
                settled = true;
                try { this.socket?.close(); } catch (e) { /* ya cerrado */ }
                reject(new Error(msg));
            };

            try {
                this.socket = new WebSocket(url);
            } catch (err) {
                // SecurityError: pagina https intentando ws://
                fail(`El navegador bloqueó ${url}: ${err.message}`);
                return;
            }

            const timer = setTimeout(() => fail(`El rover no respondió en ${url}.`), timeoutMs);

            this.socket.onopen = () => {
                clearTimeout(timer);
                if (settled) return;
                settled = true;
                resolve();
            };
            this.socket.onerror = () => { clearTimeout(timer); fail(`No se pudo abrir ${url}.`); };
            this.socket.onmessage = (ev) => {
                // El firmware manda una trama por linea; un mensaje puede traer varias.
                String(ev.data).split('\n').forEach((l) => this.onLine(l));
            };
            this.socket.onclose = () => {
                clearTimeout(timer);
                if (!settled) { fail(`Se cerró la conexión con ${url}.`); return; }
                if (!this.closing) this.onClose();
            };
        });
    }

    async send(text) {
        if (this.socket?.readyState !== WebSocket.OPEN) throw new Error('WebSocket cerrado');
        this.socket.send(text);
    }

    async close() {
        this.closing = true;
        try { this.socket?.close(); } catch (e) { /* ya cerrado */ }
        this.socket = null;
    }
}
