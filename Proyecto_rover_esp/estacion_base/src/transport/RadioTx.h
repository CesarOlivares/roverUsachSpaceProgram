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
    /** false si el modulo no responde por SPI. */
    bool begin();

    /**
     * Envia un paquete y espera el ACK (con reintentos por hardware).
     * @param ack    buffer de al menos 32 bytes para la telemetria de vuelta
     * @param ackLen bytes recibidos en el ACK; 0 si vino vacio
     * @return true si el rover confirmo la recepcion
     */
    bool send(const uint8_t *buf, uint8_t len, uint8_t *ack, uint8_t &ackLen);

private:
    RF24 radio_;
};
