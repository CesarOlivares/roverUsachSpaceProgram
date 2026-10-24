'use strict';

/* Utilidades de pintado comunes a todas las vistas. */
const Dom = {
    byId(id) { return document.getElementById(id); },

    setText(el, text) {
        if (el && el.textContent !== text) el.textContent = text;
    },

    setBadge(el, text, cls) {
        if (!el) return;
        el.textContent = text;
        el.className = `badge ${cls}`;
    },

    setPinBadge(el, text, cls) {
        if (!el) return;
        el.textContent = text;
        el.className = `pin-badge ${cls}`;
    },

    isTypingTarget(target) {
        return target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement;
    },
};

const Format = {
    uptime(seconds) {
        const s = Math.max(0, Math.floor(seconds));
        const h = Math.floor(s / 3600);
        const m = Math.floor((s % 3600) / 60);
        const sec = s % 60;
        if (h > 0) return `${h}h ${String(m).padStart(2, '0')}m`;
        if (m > 0) return `${m}m ${String(sec).padStart(2, '0')}s`;
        return `${sec}s`;
    },

    steer(angle) {
        const a = Math.round(angle);
        if (a < 80) return `⬅️ IZQ (${a}°)`;
        if (a > 100) return `➡️ DER (${a}°)`;
        return `⏺️ CENTRO (${a}°)`;
    },

    /**
     * Resumen del enlace para las vistas: texto, clase de badge y estado
     * (off / warn / ok). Por USB puede haber una estacion base con radio en
     * medio (llega su estado) o el rover enchufado directo (no llega).
     */
    linkStatus(model) {
        const { mode, stale } = model.link;
        const radio = model.radio;

        if (mode === 'none') return { text: '● DESCONECTADO', cls: 'badge-danger', state: 'off' };

        let label = mode === 'demo' ? '🧪 SIMULACION' : '🔌 CABLE DIRECTO';
        if (radio) {
            if (!radio.rover) {
                return { text: '📻 BASE OK · SIN SEÑAL DEL ROVER', cls: 'badge-warning', state: 'warn' };
            }
            label = `📻 RADIO nRF24 · ${radio.lq}%`;
        }

        if (stale) return { text: `${label} · SIN DATOS`, cls: 'badge-warning', state: 'warn' };
        if (radio && radio.lq < 70) return { text: label, cls: 'badge-warning', state: 'warn' };
        return { text: label, cls: 'badge-success', state: 'ok' };
    },
};
