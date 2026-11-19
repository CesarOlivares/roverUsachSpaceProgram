#include "PcView.h"

#include <Arduino.h>
#include <stdio.h>
#include <inttypes.h>

namespace PcView {

void sendTelemetry(const BaseModel &model)
{
    static char buf[400];
    size_t len = TelemetryJson::render(model.telemetryForPanel(), buf, sizeof(buf));
    // Una sola escritura por linea para que no se mezcle con otra.
    if (len > 0) Serial.write((const uint8_t *)buf, len);
}

void sendStatus(const BaseModel &model, bool chipOk, uint32_t nowMs)
{
    // arc: retransmisiones medias x10 · retry: intento de reconexion en curso
    // reinit: reconfiguraciones totales · chip: el nRF24 de la base responde
    // up: segundos desde el arranque de la base (si baja, la base se reinicio)
    char buf[224];
    int n = snprintf(buf, sizeof(buf),
        "{\"type\":\"base\",\"rover\":%s,\"lq\":%d,\"tx_ok\":%" PRIu32 ",\"tx_fail\":%" PRIu32
        ",\"arc\":%d,\"retry\":%" PRIu32 ",\"reinit\":%" PRIu32 ",\"chip\":%s,\"up\":%" PRIu32 "}\n",
        model.roverReachable(nowMs) ? "true" : "false",
        model.linkQuality(),
        model.sentOk(),
        model.sentFail(),
        model.avgRetriesX10(),
        model.retry(),
        model.reinits(),
        chipOk ? "true" : "false",
        nowMs / 1000);
    if (n > 0 && (size_t)n < sizeof(buf)) Serial.write((const uint8_t *)buf, n);
}

}  // namespace PcView
