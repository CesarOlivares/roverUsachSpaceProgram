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

void sendStatus(const BaseModel &model, uint32_t nowMs)
{
    char buf[128];
    int n = snprintf(buf, sizeof(buf),
        "{\"type\":\"base\",\"rover\":%s,\"lq\":%d,\"tx_ok\":%" PRIu32 ",\"tx_fail\":%" PRIu32 "}\n",
        model.roverReachable(nowMs) ? "true" : "false",
        model.linkQuality(),
        model.sentOk(),
        model.sentFail());
    if (n > 0 && (size_t)n < sizeof(buf)) Serial.write((const uint8_t *)buf, n);
}

}  // namespace PcView
