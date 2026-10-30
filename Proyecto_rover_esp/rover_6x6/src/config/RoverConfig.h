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

// Las 6 ruedas las mueve un unico BTS7960: una sola consigna de traccion. La
// telemetria sigue publicando las 6 (iguales) para que el panel las dibuje.
constexpr int kWheelCount = proto::kMotorCount;
constexpr int kServoCount = proto::kServoCount;   // canales 0..4, todos en la telemetria

// El paso a paso (DRV8825) no esta montado: su EN chocaba con el SDA del PCA9685.
// STEPPER_JOG se acepta pero no mueve nada mientras esto sea false.
constexpr bool kStepperMounted = false;

/*
 * Direccion 4WS: un servo por esquina en el PCA9685.
 *   canal 0 = FL (delantero izq)   canal 1 = FR (delantero der)
 *   canal 2 = RL (trasero izq)     canal 3 = RR (trasero der)
 * Los delanteros giran 90+delta y los traseros 90-delta, en sentido opuesto,
 * para cerrar el radio de giro (geometria de la version Arduino que funcionaba).
 * El 5.o servo (canal 4) esta en suspension: queda centrado y solo se mueve
 * desde diagnostico.
 */
constexpr int kSteeringServosInDrive = 4;

// TEST_MOTOR acepta cualquier rueda 0..5, pero todas comparten traccion:
// probar una mueve las seis.

// Rampa de traccion: la salida real persigue a la consigna a este ritmo. Seis
// motores arrancando de golpe hunden la fuente y, con ella, la radio y los
// servos. 150 %/s = de 0 a 100 en ~0,7 s. La parada de emergencia, DISARM y
// el failsafe cortan en seco, sin rampa.
constexpr float kTractionRampPctPerS = 150.0f;

// Geometria de direccion: 45 grados izq <- 90 centro -> 135 der.
constexpr float kSteerCenterDeg = 90.0f;
constexpr float kSteerThrowDeg  = 45.0f;

// Ajuste mecanico por esquina (FL, FR, RL, RR): correccion fina del centro en
// grados y sentido de giro (-1 invierte) segun como quedo montado cada servo.
constexpr float  kServoTrimDeg[kSteeringServosInDrive] = { 0.0f, 0.0f, 0.0f, 0.0f };
constexpr int8_t kServoInvert[kSteeringServosInDrive]  = { 1, 1, 1, 1 };

// Limite por orden STEPPER_JOG, para que la consigna no desborde int32.
constexpr int32_t kStepperMaxJog = 100000;

}  // namespace cfg
