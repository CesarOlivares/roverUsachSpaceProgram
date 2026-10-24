#pragma once

#include <stddef.h>
#include <stdint.h>
#include "Command.h"
#include "Telemetry.h"

/*
 * Protocolo binario del enlace nRF24L01 entre la estacion base y el rover.
 *
 * El nRF24 lleva como mucho 32 bytes por paquete, asi que por radio no viaja
 * JSON: la base traduce cada linea del panel a un paquete compacto y el rover
 * responde con la telemetria dentro del propio ACK (ack payload).
 *
 *   base (PTX) --orden-->  rover (PRX)
 *   base       <--ACK + telemetria (alterna MOVIMIENTO / ESTADO)--
 *
 * Todos los enteros van en little-endian, byte a byte (sin structs empaquetados).
 * Si cambias el formato, sube kVersion: un extremo con otra version descarta
 * los paquetes en vez de interpretarlos mal.
 */

namespace radio {

constexpr uint8_t kVersion    = 1;
constexpr uint8_t kPayloadMax = 32;

/* --- Emparejamiento: tiene que ser igual en la base y en el rover --- */

// Canal 110 = 2,510 GHz, por encima de los canales Wi-Fi (2,401..2,483 GHz).
constexpr uint8_t kChannel = 110;

// Direccion de 5 bytes del rover. Cambiala si hay otro rover en el mismo canal.
constexpr uint8_t kRoverAddress[5] = { 'R', 'V', 'R', '0', '1' };

// 250 kbps: el maximo alcance del nRF24. Las tramas son pequenas y van a 20 Hz,
// asi que la velocidad sobra. Valores validos: 250, 1000, 2000.
constexpr uint16_t kDataRateKbps = 250;

// Reloj del bus SPI con el modulo. La libreria usa 10 MHz por defecto, pero
// con jumpers Dupont de 10-15 cm el flanco se degrada y aparecen lecturas
// corruptas: el modulo "se cae" a ratos. 4 MHz sobra para 32 bytes a 20 Hz.
// Si usas cables cortos soldados puedes volver a 10000000.
constexpr uint32_t kSpiHz = 4000000;

// Potencia de emision: 0 = MIN, 1 = LOW, 2 = HIGH, 3 = MAX.
// A menos de ~2 m, sobre todo con modulos PA+LNA, HIGH/MAX saturan el
// receptor del otro lado y se pierden paquetes. Para pruebas en la mesa usa
// 1 (LOW); para salir a terreno sube a 2 o 3 (y alimenta bien el modulo).
constexpr uint8_t kPaLevel = 1;

// Reintentos automaticos de la base: espera (n+1) x 250 us entre intentos.
// A 250 kbps un ACK con 32 bytes de telemetria necesita al menos 1500 us (n = 5).
constexpr uint8_t kRetryDelay = 5;
constexpr uint8_t kRetryCount = 5;

// Id con el que el rover ve a la estacion base en el arbitraje del mando.
constexpr const char *kRadioClientId = "radio";

/* --- Tipos de paquete --- */

enum PacketType : uint8_t {
    kPktCommand         = 0x10,
    kPktTelemetryMotion = 0x20,   // armado, consignas de motores y servos, seq
    kPktTelemetryStatus = 0x21,   // uptime, paso a paso, contadores, memoria
};

// Quien tiene el mando, en 2 bits: el nombre completo no cabe en el paquete.
enum OwnerCode : uint8_t {
    kOwnerFree  = 0,
    kOwnerRadio = 1,   // la estacion base (y por tanto el panel conectado a ella)
    kOwnerOther = 2,   // otro cliente, p. ej. un panel por cable directo al rover
};

OwnerCode ownerCodeOf(const char *owner);

/** @return bytes escritos, o 0 si la orden no se puede codificar. */
size_t encodeCommand(const Command &cmd, uint8_t *out, size_t outLen);

/** Decodifica una orden. Le pone kRadioClientId como cliente. */
bool decodeCommand(const uint8_t *in, size_t len, Command &out);

/** @param motion true = paquete de movimiento, false = paquete de estado. */
size_t encodeTelemetry(const TelemetryData &t, bool motion, uint8_t *out, size_t outLen);

/**
 * Vuelca un paquete de telemetria sobre @p t (solo los campos que trae).
 * @return el tipo de paquete decodificado, o 0 si no es valido.
 */
uint8_t decodeTelemetry(const uint8_t *in, size_t len, TelemetryData &t, OwnerCode &owner);

}  // namespace radio
