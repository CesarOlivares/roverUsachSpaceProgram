#pragma once

#include "CommandController.h"

/*
 * Failsafe unico para todos los transportes: si el operador al mando deja de
 * hablar durante cfg::kFailsafeTimeoutMs, corta la potencia y libera el mando.
 */
class SafetyController {
public:
    explicit SafetyController(CommandController &ctrl) : ctrl_(ctrl) {}

    bool begin();

private:
    static void task(void *arg);
    void        loop();

    CommandController &ctrl_;
};
