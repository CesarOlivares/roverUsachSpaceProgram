#pragma once

#include "../controller/CommandController.h"

/*
 * Enlace por cable directo: lineas JSON por Serial a 115200 8N1. Es el mismo
 * formato que habla el panel con la estacion base, asi que para probar en banco
 * basta con enchufar el panel al USB del rover en vez de al de la base.
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
