#pragma once

#include "../model/BaseModel.h"

/*
 * Vista de la base: lo que ve el panel por el USB.
 *
 *   - Telemetria del rover, en la misma linea JSON que mandaria el rover por
 *     cable, mas "radio_lq". El panel no distingue de donde viene.
 *   - Estado propio de la base: {"type":"base","rover":true,"lq":95,...}
 */
namespace PcView {

void sendTelemetry(const BaseModel &model);
void sendStatus(const BaseModel &model, uint32_t nowMs);

}  // namespace PcView
