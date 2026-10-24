'use strict';

function clamp(v, lo, hi) { return Math.min(hi, Math.max(lo, v)); }
function round1(v) { return Math.round(v * 10) / 10; }
function num(v) { return typeof v === 'number' && Number.isFinite(v) ? v : 0; }
function sleep(ms) { return new Promise((r) => setTimeout(r, ms)); }

/** true si la ESP sirvio esta pagina (en vez de abrirse desde disco con file://). */
function servedFromRover() {
    return location.protocol === 'http:' || location.protocol === 'https:';
}

/** Publicador minimo: base del modelo, de las vistas y del enlace. */
class Emitter {
    constructor() { this._handlers = new Map(); }

    on(event, fn) {
        if (!this._handlers.has(event)) this._handlers.set(event, []);
        this._handlers.get(event).push(fn);
        return this;
    }

    emit(event, payload) {
        for (const fn of this._handlers.get(event) || []) fn(payload);
    }
}
