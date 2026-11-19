#pragma once

#include <stdint.h>
#include <SPI.h>
#include <RF24.h>

/*
 * nRF24L01 en modo transmisor (PTX). Cada envio espera el ACK automatico del
 * rover, que trae la telemetria dentro (ack payload).
 */
class RadioTx {
public:
    RadioTx();

    /** false si el modulo no responde por SPI. */
    bool begin();

    /** Vuelve a configurar el modulo (intento de reconexion). false si no responde. */
    bool reinit();

    /** true si el modulo responde por SPI ahora mismo. */
    bool chipConnected() { return radio_.isChipConnected(); }

    /**
     * Envia un paquete y espera el ACK (con reintentos por hardware).
     * @param ack    buffer de al menos 32 bytes para la telemetria de vuelta
     * @param ackLen bytes recibidos en el ACK; 0 si vino vacio
     * @return true si el rover confirmo la recepcion
     */
    bool send(const uint8_t *buf, uint8_t len, uint8_t *ack, uint8_t &ackLen);

    /**
     * Retransmisiones que necesito el ultimo envio (0..15). Es el mejor aviso
     * temprano de un enlace que se degrada: sube antes de que se pierdan paquetes.
     */
    uint8_t lastRetries() const { return lastArc_; }

private:
    bool configure();

    RF24    radio_;
    uint8_t lastArc_ = 0;
};
