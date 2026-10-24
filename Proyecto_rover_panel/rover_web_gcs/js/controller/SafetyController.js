'use strict';

/** Parada de emergencia: Escape, barra espaciadora, boton y cierre de pestana. */
class SafetyController {
    constructor(model, link) {
        this.model = model;
        this.link = link;
    }

    bindInputs() {
        window.addEventListener('keydown', (e) => {
            if (Dom.isTypingTarget(e.target)) return;
            if (e.code === 'Escape' || e.code === 'Space') {
                e.preventDefault();
                this.emergencyStop();
            }
        });

        /*
         * Al cerrar la pestana no hay tiempo de esperar a la cola: el STOP sale
         * directo. Si aun asi no llega, el failsafe del firmware corta en 400 ms.
         */
        window.addEventListener('beforeunload', () => {
            // Un observador que cierra su pestana no debe parar al operador al mando.
            if (!this.model.hasControl()) return;
            if (!this.model.armed && !this.model.manual) return;
            this.model.requestDisarm(performance.now());
            this.link.sendNow(Protocol.stop());
        });
    }

    /** Corte total: motores, paso a paso y armado. */
    emergencyStop() {
        this.model.requestDisarm(performance.now());
        this.model.releaseKeys();
        this.link.sendEvent(Protocol.stop());
        this.model.emit('notice', 'PARADA DE EMERGENCIA enviada.');
    }
}
