#pragma once

/*
 * Parametros del rover. Todo lo ajustable vive aqui; las credenciales del
 * SoftAP van aparte en Secrets.h para que no acaben en el repositorio.
 */

#include <stddef.h>
#include <stdint.h>

// Sin Secrets.h se compila con la clave publica de ejemplo; WebLink.cpp avisa.
#if __has_include("Secrets.h")
#include "Secrets.h"
#define ROVER_EXAMPLE_SECRETS 0
#else
#include "Secrets.example.h"
#define ROVER_EXAMPLE_SECRETS 1
#endif

namespace cfg {

/* --- Enlaces ------------------------------------------------------------ */

// Los dos transportes son independientes y pueden convivir: comparten el mismo
// controlador, la misma telemetria y el mismo failsafe.
constexpr bool kLinkSerial = true;   // linea JSON por Serial (USB, 115200 8N1)
constexpr bool kLinkWeb    = true;   // SoftAP + servidor web que sirve el panel y expone /ws

constexpr uint32_t kSerialBaud      = 115200;
constexpr uint16_t kHttpPort        = 80;
constexpr uint8_t  kApChannel       = 1;
constexpr uint8_t  kApMaxConn       = 4;
constexpr uint8_t  kWsMaxClients    = 4;   // clientes a los que se difunde la telemetria

constexpr uint8_t  kApIp[4]         = {192, 168, 4, 1};
constexpr uint8_t  kApMask[4]       = {255, 255, 255, 0};

constexpr uint32_t kTelemetryHz     = 20;
constexpr uint32_t kTelemetryPeriodMs = 1000 / kTelemetryHz;

constexpr size_t   kMaxLineLen      = 256;  // trama de mando maxima
constexpr size_t   kTelemetryBufLen = 384;

/* --- Seguridad ---------------------------------------------------------- */

/*
 * Sin trafico DEL OPERADOR AL MANDO durante este tiempo, el failsafe corta la
 * potencia y libera el mando. Los paneles emiten a 20 Hz (50 ms): 8x de margen.
 * Los latidos de un panel observador no cuentan: si contaran, un observador
 * conectado mantendria al rover andando con la ultima consigna del operador caido.
 */
constexpr uint32_t kFailsafeTimeoutMs = 400;
constexpr uint32_t kFailsafePollMs    = 25;

// Longitud maxima (con terminador) del identificador de cliente.
constexpr size_t kOwnerIdLen = 24;

/* --- Actuadores --------------------------------------------------------- */

constexpr int kMotorCount = 6;

// Canales del PCA9685 cableados a servos (0..4). Se publican todos en la telemetria.
constexpr int kServoCount = 5;

// Servos que sigue la direccion en modo DRIVE. El 5.o (canal 4) esta en
// suspension hasta nuevo aviso: queda centrado y solo se mueve desde diagnostico.
constexpr int kSteeringServosInDrive = 4;

// Geometria de direccion: 45 grados izq <- 90 centro -> 135 der.
constexpr float kSteerCenterDeg = 90.0f;
constexpr float kSteerThrowDeg  = 45.0f;

// Limite por orden STEPPER_JOG, para que la consigna no desborde int32.
constexpr int32_t kStepperMaxJog = 100000;

}  // namespace cfg
