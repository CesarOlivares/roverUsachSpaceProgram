#pragma once

#include "../controller/CommandController.h"

/*
 * Enlace por cable: lineas JSON por Serial a 115200 8N1.
 *
 * Serial es UART0 con "USB CDC On Boot: Disabled" (puerto del conversor USB) o
 * el USB nativo con "Enabled". Funciona igual en los dos casos.
 */
class SerialLink {
public:
    explicit SerialLink(CommandController &ctrl) : ctrl_(ctrl) {}

    /**
     * Abre el puerto con buffers grandes. Va lo primero en setup(): los
     * buffers solo se pueden fijar antes de Serial.begin() y el log usa Serial.
     */
    static void openPort();

    /** Lanza las tareas de recepcion y de telemetria. */
    bool begin();

private:
    static void rxTask(void *arg);
    static void telemetryTask(void *arg);
    void rxLoop();
    void telemetryLoop();

    CommandController &ctrl_;
};
