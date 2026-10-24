#pragma once

/*
 * Parametros del rover. Todo lo ajustable vive aqui. El emparejamiento de la
 * radio (canal y direccion) esta en common/RadioProtocol.h porque tiene que
 * coincidir con la estacion base.
 */

#include <stddef.h>
#include <stdint.h>
#include "../common/Command.h"

namespace cfg {

/* --- Enlaces ------------------------------------------------------------ */

// Los dos enlaces son independientes y pueden convivir: comparten el mismo
// controlador, la misma telemetria y el mismo failsafe.
constexpr bool kLinkRadio  = true;   // nRF24L01 con la estacion base (enlace principal)
constexpr bool kLinkSerial = true;   // linea JSON por USB, para pruebas en banco con cable directo

constexpr uint32_t kSerialBaud        = 115200;
constexpr uint32_t kTelemetryHz       = 20;     // cadencia de la telemetria por cable
constexpr uint32_t kTelemetryPeriodMs = 1000 / kTelemetryHz;

constexpr size_t kMaxLineLen      = 256;  // trama de mando maxima por cable
constexpr size_t kTelemetryBufLen = 384;

/* --- Seguridad ---------------------------------------------------------- */

/*
 * Sin trafico DEL OPERADOR AL MANDO durante este tiempo, el failsafe corta la
 * potencia y libera el mando. La base reenvia a 20 Hz (50 ms): 8x de margen.
 * Los latidos de un observador no cuentan: si contaran, mantendrian al rover
 * andando con la ultima consigna del operador caido.
 */
constexpr uint32_t kFailsafeTimeoutMs = 400;
constexpr uint32_t kFailsafePollMs    = 25;

constexpr size_t kOwnerIdLen = proto::kClientIdLen;

/* --- Actuadores --------------------------------------------------------- */

constexpr int kMotorCount = proto::kMotorCount;
constexpr int kServoCount = proto::kServoCount;   // canales 0..4, todos en la telemetria

// Servos que sigue la direccion en modo DRIVE. El 5.o (canal 4) esta en
// suspension hasta nuevo aviso: queda centrado y solo se mueve desde diagnostico.
constexpr int kSteeringServosInDrive = 4;

// Geometria de direccion: 45 grados izq <- 90 centro -> 135 der.
constexpr float kSteerCenterDeg = 90.0f;
constexpr float kSteerThrowDeg  = 45.0f;

// Limite por orden STEPPER_JOG, para que la consigna no desborde int32.
constexpr int32_t kStepperMaxJog = 100000;

}  // namespace cfg
