'use strict';

/**
 * Configura el Wi-Fi del rover. Las ordenes WIFI_* las atiende el rover por
 * cable directo o por Wi-Fi; la estacion base no las reenvia por radio.
 */
class WifiController {
    constructor(model, link) {
        this.model = model;
        this.link = link;
    }

    _canSend() {
        const m = this.model;
        if (!m.connected || m.link.mode === 'demo') {
            m.emit('notice', 'Conecta con el rover (Wi-Fi o cable directo) para configurar su Wi-Fi.');
            return false;
        }
        if (m.radio && m.link.mode === 'serial') {
            m.emit('notice', 'La estación base no reenvía la configuración Wi-Fi. Conecta por Wi-Fi o por cable directo al rover.');
            return false;
        }
        return true;
    }

    join({ ssid, pass }) {
        if (!this._canSend()) return;
        const m = this.model;
        if (!ssid) { m.emit('notice', 'Escribe el nombre de la red.'); return; }
        if (new TextEncoder().encode(ssid).length > 32) { m.emit('notice', 'El nombre de la red tiene como máximo 32 bytes.'); return; }
        if (pass && pass.length < 8) { m.emit('notice', 'La clave WPA tiene al menos 8 caracteres.'); return; }
        if (m.armed) { m.emit('notice', 'Desarma el rover antes de cambiar su red Wi-Fi.'); return; }

        if (m.link.mode === 'wifi' && m.wifi?.staState === 'connected' && m.wifiHost === m.wifi.staIp) {
            m.emit('notice', 'Estás conectado por la red externa: al cambiarla perderás el enlace. ' +
                `Vuelve a entrar por la red propia del rover (${m.wifi.apIp}) o por la nueva red.`);
        }
        this.link.sendEvent(Protocol.wifiSet(ssid, pass || ''));
    }

    scan() {
        if (!this._canSend()) return;
        this.link.sendEvent(Protocol.wifiScan());
    }

    forget() {
        if (!this._canSend()) return;
        const m = this.model;
        if (m.armed) { m.emit('notice', 'Desarma el rover antes de cambiar su red Wi-Fi.'); return; }
        if (!window.confirm('¿Olvidar la red guardada? El rover quedará solo con su red propia.')) return;
        this.link.sendEvent(Protocol.wifiForget());
    }
}
