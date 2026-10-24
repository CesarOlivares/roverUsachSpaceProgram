'use strict';

/**
 * Modal del asistente de pre-vuelo.
 * Intenciones: 'answer' (true = confirmar, false = cancelar).
 */
class PreflightView extends Emitter {
    constructor() {
        super();
        this.modal = Dom.byId('calibration-modal');
        this.badge = Dom.byId('modal-step-badge');
        this.title = Dom.byId('modal-step-title');
        this.desc = Dom.byId('modal-step-desc');
        this.yes = Dom.byId('btn-wizard-yes');

        this.yes?.addEventListener('click', () => this.emit('answer', true));
        Dom.byId('btn-wizard-no')?.addEventListener('click', () => this.emit('answer', false));
    }

    show(visible) {
        if (this.modal) this.modal.style.display = visible ? 'flex' : 'none';
    }

    /** html solo lleva texto generado por el propio panel, nunca datos del rover sin escapar. */
    renderStep(step, total, title, html) {
        Dom.setText(this.badge, `PASO ${step} DE ${total}`);
        Dom.setText(this.title, title);
        if (this.desc) this.desc.innerHTML = html;
    }

    /** Bloquea "Si" mientras se mueve un actuador; "Cancelar" nunca se bloquea. */
    setBusy(busy) {
        if (!this.yes) return;
        this.yes.disabled = busy;
        this.yes.textContent = busy ? 'Probando...' : 'Sí, confirmar';
    }
}
