#pragma once

#include <atomic>
#include <SPI.h>
#include <RF24.h>
#include "../controller/CommandController.h"
#include "../common/RadioProtocol.h"

/*
 * Enlace nRF24L01 con la estacion base. El rover es el receptor (PRX):
 *
 *   1. llega una orden binaria de la base -> se decodifica y va al controlador
 *   2. el rover deja cargada la telemetria en el ACK del siguiente paquete
 *
 * Asi no hace falta que el rover transmita por su cuenta ni cambiar de modo:
 * la telemetria viaja gratis en la confirmacion automatica del nRF24.
 * Alterna paquete de movimiento y de estado, que no caben juntos en 32 bytes.
 */
class RadioLink {
public:
    explicit RadioLink(CommandController &ctrl)
        : ctrl_(ctrl), spi_(FSPI), radio_(radio::kSpiHz) {}

    /** false si el modulo no responde por SPI (el rover sigue por cable). */
    bool begin();

    /**
     * Pide reconfigurar el nRF24 (intento de reconexion del failsafe). Se puede
     * llamar desde cualquier tarea: lo ejecuta la tarea de la radio.
     */
    void requestReinit();

private:
    static void task(void *arg);
    void        loop();
    bool        configure();
    void        loadAckPayload();

    CommandController &ctrl_;
    SPIClass           spi_;
    RF24               radio_;
    bool               nextIsMotion_ = true;
    std::atomic<bool>  reinitRequested_{false};
};
