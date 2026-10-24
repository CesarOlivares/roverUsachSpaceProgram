#include "TelemetryView.h"

#include <stdio.h>
#include <inttypes.h>

static_assert(cfg::kMotorCount == 6, "El formato de telemetria asume 6 motores");
static_assert(cfg::kServoCount == 5, "El formato de telemetria asume 5 servos");

namespace TelemetryView {

size_t render(const RoverSnapshot &s, int clients, char *out, size_t outLen)
{
    // El owner es seguro de incrustar tal cual: el modelo solo admite
    // alfanumericos, asi que no puede romper la trama de los demas clientes.
    int n = snprintf(out, outLen,
        "{\"type\":\"telemetry\",\"seq\":%" PRIu32
        ",\"uptime\":%" PRIu32
        ",\"armed\":%s"
        ",\"owner\":\"%s\""
        ",\"motors\":[%.1f,%.1f,%.1f,%.1f,%.1f,%.1f]"
        ",\"servos\":[%.0f,%.0f,%.0f,%.0f,%.0f]"
        ",\"stepper_pos\":%" PRId32 ",\"stepper_moving\":%s"
        ",\"rx_ok\":%" PRIu32 ",\"rx_err\":%" PRIu32
        ",\"clients\":%d,\"heap\":%" PRIu32 "}\n",
        s.seq,
        s.uptimeS,
        s.armed ? "true" : "false",
        s.owner,
        s.motors[0], s.motors[1], s.motors[2], s.motors[3], s.motors[4], s.motors[5],
        s.servos[0], s.servos[1], s.servos[2], s.servos[3], s.servos[4],
        s.stepperPos,
        s.stepperMoving ? "true" : "false",
        s.rxOk, s.rxErr,
        clients,
        s.freeHeap);

    if (n <= 0 || (size_t)n >= outLen) return 0;   // truncada: mejor no mandar nada
    return (size_t)n;
}

}  // namespace TelemetryView
