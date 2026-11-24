'use strict';

/**
 * Tarjeta del Wi-Fi del rover (pestana Diagnostico).
 * Intenciones: 'join' ({ ssid, pass }), 'scan', 'forget'.
 */
class WifiView extends Emitter {
    constructor(model) {
        super();
        this.model = model;
        this.ap = Dom.byId('wifi-ap');
        this.sta = Dom.byId('wifi-sta');
        this.staIp = Dom.byId('wifi-sta-ip');
        this.msg = Dom.byId('wifi-msg');
        this.ssid = Dom.byId('wifi-ssid');
        this.pass = Dom.byId('wifi-pass');
        this.list = Dom.byId('wifi-scan-list');
        this.scanBtn = Dom.byId('btn-wifi-scan');
        this.buttons = [...document.querySelectorAll('[data-wifi-action]')];

        Dom.byId('wifi-form')?.addEventListener('submit', (ev) => {
            ev.preventDefault();
            this.emit('join', { ssid: this.ssid.value.trim(), pass: this.pass.value });
        });
        this.scanBtn?.addEventListener('click', () => this.emit('scan'));
        Dom.byId('btn-wifi-forget')?.addEventListener('click', () => this.emit('forget'));
        this.list?.addEventListener('click', (ev) => {
            const li = ev.target.closest('[data-ssid]');
            if (!li) return;
            this.ssid.value = li.dataset.ssid;
            this.pass.value = '';
            (li.dataset.open === 'true' ? this.ssid : this.pass).focus();
        });

        model.on('wifi', () => this.render());
        model.on('link', () => this.render());
        this.render();
    }

    static signal(rssi) {
        if (rssi === null || rssi === 0) return '';
        const q = rssi >= -55 ? 'excelente' : rssi >= -67 ? 'buena' : rssi >= -75 ? 'regular' : 'débil';
        return `${rssi} dBm · ${q}`;
    }

    render() {
        const w = this.model.wifi;
        const usable = this.model.connected && this.model.link.mode !== 'demo';
        this.buttons.forEach((b) => { b.disabled = !w || !usable; });

        if (!w) {
            Dom.setText(this.ap, '--');
            Dom.setText(this.sta, '--');
            if (this.sta) this.sta.dataset.state = 'off';
            Dom.setText(this.staIp, '--');
            Dom.setText(this.msg, usable
                ? (this.model.radio
                    ? 'La estación base no reenvía el Wi-Fi: para configurarlo, conecta por Wi-Fi o por cable directo al rover.'
                    : 'Esperando el estado del Wi-Fi del rover...')
                : 'Conecta por Wi-Fi o por cable directo al rover para ver y cambiar su red.');
            this._renderScan();
            return;
        }

        Dom.setText(this.ap, `${w.ap} · ${w.apIp} · ${w.apClients} conectado${w.apClients === 1 ? '' : 's'}`);

        const states = { connected: 'CONECTADO', connecting: 'CONECTANDO...', waiting: 'NO DISPONIBLE', off: 'NINGUNA' };
        const label = w.sta ? `${w.sta} · ${states[w.staState] || w.staState}` : 'Ninguna (solo red propia)';
        Dom.setText(this.sta, label);
        if (this.sta) this.sta.dataset.state = { connected: 'ok', connecting: 'warn', waiting: 'bad' }[w.staState] || 'off';

        Dom.setText(this.staIp, w.staState === 'connected'
            ? `http://${w.staIp}/ · ${w.host} · ${WifiView.signal(w.rssi)}`
            : '--');
        Dom.setText(this.msg, w.msg || '');
        if (this.scanBtn) this.scanBtn.textContent = w.scanning ? 'Buscando...' : 'Buscar redes';
        this._renderScan();
    }

    _renderScan() {
        if (!this.list) return;
        const nets = this.model.wifiScan;
        if (!nets) { this.list.replaceChildren(); return; }
        if (nets.length === 0) {
            const li = document.createElement('li');
            li.className = 'wifi-net-empty';
            li.textContent = 'No se encontraron redes.';
            this.list.replaceChildren(li);
            return;
        }
        this.list.replaceChildren(...nets.map((n) => {
            const li = document.createElement('li');
            li.dataset.ssid = n.ssid;
            li.dataset.open = String(n.open);
            li.tabIndex = 0;
            const name = document.createElement('strong');
            name.textContent = n.ssid;
            const info = document.createElement('span');
            info.textContent = `${n.open ? 'abierta' : 'con clave'} · canal ${n.ch} · ${WifiView.signal(n.rssi)}`;
            li.append(name, info);
            return li;
        }));
    }
}
