// COPIA GENERADA de Proyecto_rover_esp/common/ - NO EDITAR AQUI.
// Edita el original y ejecuta tools/sync_common.ps1.

#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Orden de mando ya decodificada. Es lo unico que el controlador del rover
 * conoce del protocolo: los transportes traducen bytes a Command y el formato
 * de cable (JSON por USB, binario por nRF24) no sale de transport/.
 */

namespace proto {

constexpr int    kMotorCount  = 6;
constexpr int    kServoCount  = 5;    // canales de servo que publica la telemetria
constexpr size_t kClientIdLen = 24;   // id de cliente con terminador

}  // namespace proto

enum class CommandType : uint8_t {
    Heartbeat  = 0,
    Arm        = 1,
    Disarm     = 2,
    Stop       = 3,
    Drive      = 4,
    TestMotor  = 5,
    ServoSet   = 6,
    StepperJog = 7,
    Unknown    = 0xFF,
};

struct Command {
    CommandType type = CommandType::Unknown;

    // Id del cliente, saneado a alfanumericos. Cadena vacia = consola anonima.
    char client[proto::kClientIdLen] = {0};

    bool     hasSeq = false;
    uint32_t seq    = 0;

    // false si a la orden le faltan campos obligatorios: se cuenta como trama
    // recibida (renueva el enlace) pero no mueve nada.
    bool argsOk = false;

    // Drive
    float linear  = 0.0f;
    float angular = 0.0f;

    // TestMotor
    int   motor = -1;
    float speed = 0.0f;

    // ServoSet
    int   channel = -1;
    float angle   = 0.0f;

    // StepperJog
    int32_t  steps  = 0;
    uint32_t stepHz = 0;   // 0 = velocidad por defecto del driver
};
