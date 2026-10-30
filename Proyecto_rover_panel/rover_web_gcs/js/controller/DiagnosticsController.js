'use strict';

/** Pruebas individuales de motores, servos y paso a paso. */
class DiagnosticsController {
    constructor(model, link, drive) {
        this.model = model;
        this.link = link;
        this.drive = drive;
    }

    /**
     * Entra en prueba manual de un motor.
     *
     * Mientras dura, el lazo TX deja de emitir DRIVE, asi que el firmware
     * conservaria la ultima consigna de conduccion y las otras cinco ruedas
     * seguirian girando. Por eso se desarma primero: una prueba individual
     * solo tiene sentido con el rover quieto.
     */
    beginManualTest(motor, speed) {
        const m = this.model;
        if (m.armed || m.armPending?.target) {
            this.drive.disarm();
            m.emit('notice', 'Rover desarmado para ejecutar la prueba individual.');
        }
        m.setManual({ motor: Number(motor), speed: Number(speed) });
    }

    /** Suelta la prueba manual y manda un cero explicito (no depende del failsafe). */
    releaseManualTest() {
        const manual = this.model.manual;
        if (!manual) return;
        this.model.setManual(null);
        this.link.sendEvent(Protocol.testMotor(manual.motor, 0));
    }

    /** Deslizador de motor del panel de diagnostico. */
    setMotor(motor, speed) {
        if (speed === 0) {
            this.model.setManual(null);
            this.link.sendEvent(Protocol.testMotor(motor, 0));
        } else {
            this.beginManualTest(motor, speed);
        }
    }

    /** Para la traccion: un solo puente H, una sola orden. */
    stopAllMotors() {
        this.model.setManual(null);
        this.link.sendEvent(Protocol.testMotor(0, 0));
    }

    setServo(channel, angle) {
        this.link.sendEvent(Protocol.servoSet(Number(channel), Number(angle)));
    }

    jogStepper(steps) {
        this.link.sendEvent(Protocol.stepperJog(Number(steps), 1000));
    }
}
