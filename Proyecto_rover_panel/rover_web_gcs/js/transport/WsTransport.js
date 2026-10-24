'use strict';

/** Wi-Fi, WebSocket contra el SoftAP del ESP32-S3. */
class WsTransport {
    constructor(onLine, onClose) {
        this.onLine = onLine;
        this.onClose = onClose;
        this.socket = null;
    }

    open(ip, port) {
        return new Promise((resolve, reject) => {
            const url = `ws://${ip}:${port}/ws`;
            this.socket = new WebSocket(url);

            this.socket.onopen = () => resolve();
            this.socket.onerror = () => reject(new Error(`No se pudo abrir ${url}`));
            this.socket.onmessage = (ev) => {
                // El firmware manda una trama por linea; el WebSocket puede agrupar.
                String(ev.data).split('\n').forEach((l) => this.onLine(l));
            };
            this.socket.onclose = () => this.onClose();
        });
    }

    async send(text) {
        if (this.socket?.readyState !== WebSocket.OPEN) throw new Error('WebSocket cerrado');
        this.socket.send(text);
    }

    async close() {
        this.socket?.close();
        this.socket = null;
    }
}
