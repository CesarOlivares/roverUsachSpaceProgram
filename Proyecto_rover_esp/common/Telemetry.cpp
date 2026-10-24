#include "Telemetry.h"

#include <stdio.h>
#include <inttypes.h>

static_assert(proto::kMotorCount == 6, "El formato de telemetria asume 6 motores");
static_assert(proto::kServoCount == 5, "El formato de telemetria asume 5 servos");

namespace TelemetryJson {

size_t render(const TelemetryData &t, char *out, size_t outLen)
{
    // El owner es seguro de incrustar tal cual: solo admite alfanumericos, asi
    // que no puede romper la trama de los demas clientes.
    int n = snprintf(out, outLen,
        "{\"type\":\"telemetry\",\"seq\":%" PRIu32
        ",\"uptime\":%" PRIu32
        ",\"armed\":%s"
        ",\"owner\":\"%s\""
        ",\"motors\":[%.1f,%.1f,%.1f,%.1f,%.1f,%.1f]"
        ",\"servos\":[%.0f,%.0f,%.0f,%.0f,%.0f]"
        ",\"stepper_pos\":%" PRId32 ",\"stepper_moving\":%s"
        ",\"rx_ok\":%" PRIu32 ",\"rx_err\":%" PRIu32
        ",\"clients\":%d,\"heap\":%" PRIu32,
        t.seq,
        t.uptimeS,
        t.armed ? "true" : "false",
        t.owner,
        t.motors[0], t.motors[1], t.motors[2], t.motors[3], t.motors[4], t.motors[5],
        t.servos[0], t.servos[1], t.servos[2], t.servos[3], t.servos[4],
        t.stepperPos,
        t.stepperMoving ? "true" : "false",
        t.rxOk, t.rxErr,
        t.clients,
        t.freeHeap);
    if (n <= 0 || (size_t)n >= outLen) return 0;   // truncada: mejor no mandar nada

    int m = (t.radioLq >= 0)
        ? snprintf(out + n, outLen - n, ",\"radio_lq\":%d}\n", t.radioLq)
        : snprintf(out + n, outLen - n, "}\n");
    if (m <= 0 || (size_t)(n + m) >= outLen) return 0;
    return (size_t)(n + m);
}

}  // namespace TelemetryJson
