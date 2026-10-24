'use strict';

/** Cable USB, API Web Serial. Solo Chrome/Edge sobre http(s) o file://. */
class SerialTransport {
    constructor(onLine, onClose) {
        this.onLine = onLine;
        this.onClose = onClose;
        this.port = null;
        this.writer = null;
        this.reader = null;
        this.closing = false;
    }

    static get available() { return 'serial' in navigator; }

    async open() {
        if (!SerialTransport.available) {
            throw new Error('Este navegador no soporta Web Serial. Usa Chrome o Edge.');
        }
        this.port = await navigator.serial.requestPort();
        await this.port.open({ baudRate: CONFIG.BAUD_RATE });
        this.writer = this.port.writable.getWriter();
        this._readLoop();
    }

    async _readLoop() {
        const decoder = new TextDecoderStream();
        this.port.readable.pipeTo(decoder.writable).catch(() => { /* cierre normal */ });
        this.reader = decoder.readable.getReader();

        let buffer = '';
        try {
            while (true) {
                const { value, done } = await this.reader.read();
                if (done) break;
                if (!value) continue;

                buffer += value;
                const lines = buffer.split('\n');
                buffer = lines.pop();            // la ultima queda incompleta

                for (const line of lines) this.onLine(line);

                // Sin fin de linea a la vista es que llega ruido: se descarta
                // el buffer para no crecer sin limite.
                if (buffer.length > 4096) buffer = '';
            }
        } catch (err) {
            if (!this.closing) console.error('Fallo leyendo el puerto serie:', err);
        } finally {
            this.onClose();
        }
    }

    async send(text) {
        if (!this.writer) throw new Error('Puerto serie cerrado');
        await this.writer.write(new TextEncoder().encode(text));
    }

    async close() {
        this.closing = true;
        try { await this.reader?.cancel(); } catch (e) { /* ya cerrado */ }
        try { this.writer?.releaseLock(); } catch (e) { /* ya liberado */ }
        try { await this.port?.close(); } catch (e) { /* ya cerrado */ }
        this.writer = null;
    }
}
