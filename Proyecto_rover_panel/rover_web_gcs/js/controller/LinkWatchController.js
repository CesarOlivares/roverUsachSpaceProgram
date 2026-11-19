'use strict';

/**
 * Vigila la telemetria del rover y gestiona la reconexion por radio.
 *
 * Antes, una perdida de enlace desarmaba el rover a los 400 ms. Ahora:
 *   - a los FAILSAFE_MS sin telemetria el rover ya corto la traccion (lo hace
 *     su firmware), pero sigue armado;
 *   - el panel muestra "RECONECTANDO n/3" y sigue mandando sus tramas: cada
 *     trama es un intento, y la base y el rover reconfiguran su radio en cada
 *     ventana de RETRY_WINDOW_MS;
 *   - si vuelve la telemetria, se sigue conduciendo sin rearmar;
 *   - si pasan los RETRY_COUNT intentos, se manda DISARM y se desconecta.
 *
 * La perdida del USB (base desenchufada) la reintenta ConnectionController;
 * aqui solo se mira la radio.
 */
class LinkWatchController {
    constructor(model, link, connection) {
        this.model = model;
        this.link = link;
        this.connection = connection;
        this.timer = null;
    }

    start() {
        this.stop();
        this.timer = setInterval(() => this.check(performance.now()), 100);
    }

    stop() {
        if (this.timer) clearInterval(this.timer);
        this.timer = null;
    }

    check(now) {
        const m = this.model;
        // Sin transporte (USB caido) la reconexion la lleva ConnectionController.
        if (!m.connected || !this.link.connected) return;
        if (m.reconnect?.cause === 'usb') return;
        // Hasta que llega la primera telemetria no hay enlace que perder.
        if (m.lastRxAt === 0) return;

        const silence = now - m.lastRxAt;
        if (silence <= CONFIG.FAILSAFE_MS) {
            if (m.reconnect) {
                const tries = m.reconnect.attempt;
                m.setReconnect(null);
                m.emit('notice', `Enlace recuperado en el intento ${tries}. El rover sigue ${m.armed ? 'armado' : 'desarmado'}.`);
            }
            return;
        }

        const elapsed = silence - CONFIG.FAILSAFE_MS;
        const attempt = 1 + Math.floor(elapsed / CONFIG.RETRY_WINDOW_MS);

        if (attempt > CONFIG.RETRY_COUNT) {
            this.giveUp();
            return;
        }
        m.setReconnect({ attempt, cause: 'radio', since: m.lastRxAt + CONFIG.FAILSAFE_MS });
    }

    giveUp() {
        // DISARM explicito por si la radio vuelve justo ahora: el rover no debe
        // quedarse armado esperando a un panel que ya se fue.
        this.link.sendEvent(Protocol.disarm());
        this.model.emit('link-giveup', { cause: 'radio' });
        this.connection.disconnect(
            `Se perdió el enlace con el rover: ${CONFIG.RETRY_COUNT} intentos de reconexión sin respuesta. ` +
            'El rover cortó la tracción y se desarmó.');
    }
}
