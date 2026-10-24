// COPIA GENERADA de Proyecto_rover_esp/common/ - NO EDITAR AQUI.
// Edita el original y ejecuta tools/sync_common.ps1.

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "Command.h"

/*
 * Estado del rover tal como lo ven los paneles. Lo rellena el rover a partir
 * de su modelo, y la estacion base a partir de lo que recibe por radio.
 */
struct TelemetryData {
    uint32_t seq;           // ultima secuencia recibida del cliente, para medir RTT
    uint32_t uptimeS;
    bool     armed;
    char     owner[proto::kClientIdLen];
    float    motors[proto::kMotorCount];
    float    servos[proto::kServoCount];
    int32_t  stepperPos;
    bool     stepperMoving;
    uint32_t rxOk;
    uint32_t rxErr;
    uint32_t freeHeap;
    int      clients;       // enlaces activos al otro lado
    int      radioLq;       // calidad del enlace radio en %, -1 = no aplica (cable directo)
};

namespace TelemetryJson {

/**
 * Serializa en una linea JSON terminada en '\n', con snprintf sobre el buffer
 * del llamador: sin malloc en el lazo de 20 Hz.
 * @return bytes escritos, siempre < outLen; 0 si no cabe.
 */
size_t render(const TelemetryData &t, char *out, size_t outLen);

}  // namespace TelemetryJson
