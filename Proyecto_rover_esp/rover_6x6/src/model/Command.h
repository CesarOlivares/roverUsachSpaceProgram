#pragma once

#include <stdint.h>
#include "../config/RoverConfig.h"

/*
 * Orden de mando ya decodificada. Es lo unico que el controlador conoce del
 * protocolo: los transportes traducen bytes a Command y el formato de cable
 * (JSON hoy, i-BUS manana) no sale de transport/.
 */

enum class CommandType : uint8_t {
    Heartbeat,
    Arm,
    Disarm,
    Stop,
    Drive,
    TestMotor,
    ServoSet,
    StepperJog,
    Unknown,
};

struct Command {
    CommandType type = CommandType::Unknown;

    // Id del cliente, saneado a alfanumericos. Cadena vacia = consola anonima.
    char client[cfg::kOwnerIdLen] = {0};

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
