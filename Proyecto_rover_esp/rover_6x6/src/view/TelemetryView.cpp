#include "TelemetryView.h"

#include "../common/RadioProtocol.h"

namespace TelemetryView {

size_t renderJson(const RoverSnapshot &s, char *out, size_t outLen)
{
    return TelemetryJson::render(s, out, outLen);
}

size_t renderRadio(const RoverSnapshot &s, bool motion, uint8_t *out, size_t outLen)
{
    return radio::encodeTelemetry(s, motion, out, outLen);
}

}  // namespace TelemetryView
