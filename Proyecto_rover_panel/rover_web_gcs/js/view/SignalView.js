'use strict';

/**
 * Apartado "Señal": estado actual de la antena, graficas de los ultimos
 * minutos, diagnostico automatico y registro de cortes.
 * Intenciones: 'export', 'simulate' { ms }.
 */
class SignalView extends Emitter {
    constructor(model, stats) {
        super();
        this.model = model;
        this.stats = stats;

        this.kpi = {};
        document.querySelectorAll('[data-kpi]').forEach((el) => { this.kpi[el.dataset.kpi] = el; });
        this.charts = {
            lq: Dom.byId('chart-lq'),
            latency: Dom.byId('chart-latency'),
            arc: Dom.byId('chart-arc'),
            rate: Dom.byId('chart-rate'),
        };
        this.diag = Dom.byId('signal-diagnosis');
        this.log = Dom.byId('signal-log');
        this.demoTools = Dom.byId('signal-demo-tools');

        Dom.byId('btn-signal-export')?.addEventListener('click', () => this.emit('export'));
        document.querySelectorAll('[data-simulate]').forEach((btn) => {
            btn.addEventListener('click', () => this.emit('simulate', { ms: Number(btn.dataset.simulate) }));
        });

        stats.on('sample', () => this.render());
        stats.on('event', () => this.renderLog());
        stats.on('reset', () => { this.render(); this.renderLog(); });
        model.on('link', () => this.renderKpis());

        this.render();
        this.renderLog();
    }

    render() {
        this.renderKpis();
        this.renderCharts();
        this.renderDiagnosis();
        if (this.demoTools) this.demoTools.hidden = this.model.link.mode !== 'demo';
    }

    _set(key, text, state) {
        const el = this.kpi[key];
        if (!el) return;
        Dom.setText(el, text);
        el.dataset.state = state || '';
    }

    renderKpis() {
        const m = this.model;
        const s = this.stats;
        const b = m.radio;
        const status = Format.linkStatus(m);

        this._set('status', m.connected ? status.text : 'DESCONECTADO', status.state);

        const lq = b ? b.lq : null;
        this._set('lq', lq === null ? '--' : `${lq}%`, lq === null ? '' : lq >= 90 ? 'ok' : lq >= 70 ? 'warn' : 'bad');

        const lat = m.link.latency;
        this._set('latency', lat === null ? '--' : `${lat} ms`, lat === null ? '' : lat <= 80 ? 'ok' : lat <= 150 ? 'warn' : 'bad');

        const arc = b?.arc ?? null;
        this._set('arc', arc === null ? '--' : arc.toFixed(1), arc === null ? '' : arc < 1 ? 'ok' : arc < 2 ? 'warn' : 'bad');

        const rate = s.last?.rate ?? null;
        this._set('rate', rate === null ? '--' : `${rate.toFixed(0)} Hz`, rate === null ? '' : rate >= 15 ? 'ok' : rate >= 8 ? 'warn' : 'bad');

        const loss = s.lossPercent();
        this._set('loss', loss === null ? '--' : `${loss.toFixed(1)}%`, loss === null ? '' : loss < 2 ? 'ok' : loss < 10 ? 'warn' : 'bad');

        this._set('drops', String(s.drops), s.drops === 0 ? 'ok' : 'warn');
        this._set('longest', s.longestDropMs ? `${(s.longestDropMs / 1000).toFixed(1)} s` : '—', '');
        this._set('resets', `${s.roverResets} / ${s.baseResets}`, s.roverResets + s.baseResets === 0 ? 'ok' : 'bad');

        const chip = b?.chip ?? null;
        this._set('chip', chip === null ? '--' : chip ? 'RESPONDE' : 'NO RESPONDE', chip === null ? '' : chip ? 'ok' : 'bad');
        this._set('reinit', b?.reinit === null || b?.reinit === undefined ? '--' : String(b.reinit), '');
        this._set('uptime', b?.up === null || b?.up === undefined ? '--' : Format.uptime(b.up), '');
    }

    renderCharts() {
        const samples = this.stats.samples;
        this._chart(this.charts.lq, samples, (s) => s.lq, { min: 0, max: 100, unit: '%', threshold: 70 });
        this._chart(this.charts.latency, samples, (s) => s.latency, { min: 0, max: 200, unit: 'ms', threshold: 150 });
        this._chart(this.charts.arc, samples, (s) => s.arc, { min: 0, max: 5, unit: '', threshold: 2 });
        this._chart(this.charts.rate, samples, (s) => s.rate, { min: 0, max: 25, unit: 'Hz' });
    }

    /** Grafica de linea en SVG; las franjas rojas son tramos sin enlace. */
    _chart(svg, samples, pick, { min, max, unit, threshold }) {
        if (!svg) return;
        const W = 600, H = 120, n = Math.round(CONFIG.HISTORY_S * 1000 / CONFIG.SAMPLE_MS);
        const x = (i) => W - (samples.length - 1 - i) * (W / (n - 1));
        const y = (v) => H - 4 - ((clamp(v, min, max) - min) / (max - min)) * (H - 8);

        let lost = '';
        let pts = '';
        let segs = [];
        samples.forEach((s, i) => {
            if (s.lost) lost += `<rect x="${(x(i) - W / n).toFixed(1)}" y="0" width="${(W / n + 1).toFixed(1)}" height="${H}" class="chart-lost"/>`;
            const v = pick(s);
            if (v === null || v === undefined) {
                if (pts) { segs.push(pts); pts = ''; }
            } else {
                pts += `${x(i).toFixed(1)},${y(v).toFixed(1)} `;
            }
        });
        if (pts) segs.push(pts);

        const line = segs.map((p) => `<polyline points="${p.trim()}" class="chart-line"/>`).join('');
        const th = threshold !== undefined
            ? `<line x1="0" x2="${W}" y1="${y(threshold)}" y2="${y(threshold)}" class="chart-threshold"/>` : '';
        const last = [...samples].reverse().find((s) => pick(s) !== null && pick(s) !== undefined);
        const label = last ? `${Math.round(pick(last) * 10) / 10}${unit ? ' ' + unit : ''}` : '--';

        svg.innerHTML = `${lost}${th}${line}`;
        const cur = svg.parentElement?.querySelector('.chart-current');
        Dom.setText(cur, label);
    }

    renderDiagnosis() {
        if (!this.diag) return;
        this.diag.innerHTML = this.stats.diagnose().map((d) =>
            `<li class="finding" data-level="${d.level}"><strong>${escapeHtml(d.title)}</strong><span>${escapeHtml(d.text)}</span></li>`).join('');
    }

    renderLog() {
        if (!this.log) return;
        const ev = this.stats.events;
        this.log.innerHTML = ev.length === 0
            ? '<tr><td colspan="3" class="log-empty">Sin eventos todavía.</td></tr>'
            : ev.slice(0, 60).map((e) =>
                `<tr data-level="${e.level}"><td class="log-time">${e.at.toLocaleTimeString()}</td>` +
                `<td><span class="log-dot"></span>${escapeHtml(e.title)}</td><td class="log-text">${escapeHtml(e.text)}</td></tr>`).join('');
    }
}

function escapeHtml(s) {
    return String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
}
