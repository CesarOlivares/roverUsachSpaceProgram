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

    linkLabel(mode) {
        return {
            serial: '🔌 CABLE USB',
            ws: '📡 WI-FI',
            demo: '🧪 SIMULACION',
            none: '● DESCONECTADO',
        }[mode];
    },
};
