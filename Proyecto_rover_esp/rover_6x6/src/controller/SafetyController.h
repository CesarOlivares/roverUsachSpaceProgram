#pragma once

#include "CommandController.h"

/*
 * Failsafe unico para todos los transportes.
 *
 * Si el operador al mando deja de hablar durante cfg::kFailsafeTimeoutMs:
 *   1. corta la traccion en seco, pero conserva armado y mando;
 *   2. hace cfg::kLinkRetryCount intentos de reconexion (reinicia la radio);
 *   3. si en ninguno vuelve el operador, desarma y libera el mando.
 * Si el operador vuelve durante los intentos, el rover sigue armado.
 */
class SafetyController {
public:
    using ReconnectHook = void (*)();

    explicit SafetyController(CommandController &ctrl) : ctrl_(ctrl) {}

    /** Accion de cada intento de reconexion (p. ej. reiniciar el nRF24). */
    void setReconnectHook(ReconnectHook hook) { reconnect_ = hook; }

    bool begin();

private:
    static void task(void *arg);
    void        loop();
    void        attempt(uint32_t n);

    CommandController &ctrl_;
    ReconnectHook      reconnect_ = nullptr;
};
