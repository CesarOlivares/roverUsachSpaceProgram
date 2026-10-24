#pragma once

#include <stdint.h>
#include <stddef.h>
#include "../config/RoverConfig.h"

/*
 * Estado unico del rover: armado, mando, salud del enlace y consignas vigentes
 * de los actuadores. Es la fuente de la telemetria.
 *
 * No es thread-safe por si mismo: solo lo toca CommandController, que serializa
 * todo acceso (ordenes, failsafe y telemetria) con su mutex. El tiempo entra
 * como parametro para que el modelo no dependa del reloj de la placa.
 */

struct RoverSnapshot {
    uint32_t seq;
    uint32_t uptimeS;
    bool     armed;
    char     owner[cfg::kOwnerIdLen];
    float    motors[cfg::kMotorCount];
    float    servos[cfg::kServoCount];
    int32_t  stepperPos;
    bool     stepperMoving;
    uint32_t rxOk;
    uint32_t rxErr;
    uint32_t freeHeap;
};

class RoverModel {
public:
    RoverModel();

    /* --- Armado --- */
    bool armed() const { return armed_; }
    void setArmed(bool armed) { armed_ = armed; }

    /* --- Mando exclusivo ---
     *
     * Con el panel web puede haber varios clientes conectados a la vez, y todos
     * emiten a 20 Hz. El primero que actua se queda el mando; los demas siguen
     * viendo la telemetria pero no pueden mover nada. Parar lo puede cualquiera.
     *
     * Un cliente sin identificar (consola anonima por cable) puede actuar si el
     * mando esta libre, pero no lo reclama: asi no bloquea al resto para siempre.
     */
    bool hasOwner() const { return owner_[0] != '\0'; }
    const char *owner() const { return owner_; }
    bool claimControl(const char *client);
    void releaseControl() { owner_[0] = '\0'; }

    /* --- Salud del enlace --- */

    /**
     * Registra una trama valida. Solo renueva la marca de vida del operador si
     * viene del dueno del mando, o de cualquiera mientras el mando este libre.
     */
    void notePacket(const char *client, bool hasSeq, uint32_t seq, int64_t nowUs);
    void noteInvalidFrame() { rxErr_++; }

    /** Microsegundos desde la ultima trama del operador que tiene el mando. */
    int64_t controlSilenceUs(int64_t nowUs) const { return nowUs - lastControlUs_; }
    void    resetControlTimer(int64_t nowUs) { lastControlUs_ = nowUs; }

    /* --- Consignas vigentes de los actuadores --- */
    void  setMotor(int idx, float speed);
    float motor(int idx) const { return motors_[idx]; }
    bool  anyMotorActive() const;
    void  setServo(int ch, float angle);

    /** Rellena la parte del snapshot que vive en el modelo. */
    void fillSnapshot(RoverSnapshot &out) const;

private:
    bool     armed_;
    char     owner_[cfg::kOwnerIdLen];
    int64_t  lastControlUs_;
    uint32_t rxOk_;
    uint32_t rxErr_;
    uint32_t lastSeq_;    // secuencia del cliente, se devuelve para medir RTT
    float    motors_[cfg::kMotorCount];
    float    servos_[cfg::kServoCount];
};
