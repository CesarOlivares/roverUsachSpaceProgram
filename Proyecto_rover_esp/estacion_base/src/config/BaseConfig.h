#pragma once

/*
 * Parametros de la estacion base. El emparejamiento de la radio (canal y
 * direccion) esta en common/RadioProtocol.h porque tiene que coincidir con el rover.
 */

#include <stddef.h>
#include <stdint.h>
#include "sdkconfig.h"   // CONFIG_IDF_TARGET_*: para elegir los pines segun la placa

namespace cfg {

constexpr uint32_t kSerialBaud = 115200;   // igual que CONFIG.BAUD_RATE del panel
constexpr size_t   kMaxLineLen = 256;

// Sin ACK del rover durante este tiempo, la base lo da por perdido.
constexpr uint32_t kRoverLostMs   = 500;
// Cada cuanto informa la base de su propio estado al panel.
constexpr uint32_t kStatusPeriodMs = 500;
// Ventana para la calidad del enlace: ultimos N envios.
constexpr int      kLqWindow = 20;

}  // namespace cfg

/*
 * Pines del nRF24L01. Se eligen solos segun la placa en la que compiles.
 * El modulo va a 3,3 V (NUNCA a 5 V) con un condensador de 10..100 uF entre
 * VCC y GND junto al modulo.
 */
namespace pins {

#if CONFIG_IDF_TARGET_ESP32S3
// ESP32-S3: los mismos que el rover (FSPI nativo).
constexpr int kNrfCsn = 10, kNrfMosi = 11, kNrfSck = 12, kNrfMiso = 13, kNrfCe = 14;
#elif CONFIG_IDF_TARGET_ESP32
// ESP32 clasico (DevKit V1 / WROOM-32): bus VSPI por defecto.
constexpr int kNrfCsn = 5, kNrfMosi = 23, kNrfSck = 18, kNrfMiso = 19, kNrfCe = 4;
#else
#error "Placa no prevista: define aqui los pines del nRF24L01 para tu ESP32."
#endif

}  // namespace pins
