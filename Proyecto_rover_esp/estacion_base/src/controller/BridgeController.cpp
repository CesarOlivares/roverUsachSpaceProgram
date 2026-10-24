#include "BridgeController.h"

#include "../common/JsonProtocol.h"
#include "../common/RadioProtocol.h"
#include "../view/PcView.h"

void BridgeController::onPanelLine(const char *line, uint32_t nowMs)
{
    Command cmd;
    if (!JsonProtocol::parse(line, cmd)) {
        model_.notePanelError();
        return;
    }
    model_.notePanelClient(cmd.client);

    uint8_t pkt[radio::kPayloadMax];
    size_t  len = radio::encodeCommand(cmd, pkt, sizeof(pkt));
    if (len == 0) return;                       // accion desconocida: no se reenvia

    uint8_t ack[radio::kPayloadMax];
    uint8_t ackLen = 0;
    bool delivered = radio_.send(pkt, (uint8_t)len, ack, ackLen);
    model_.recordSend(delivered, nowMs);

    // Cada ACK con telemetria se reenvia al panel en el acto: la cadencia de la
    // telemetria es la de las ordenes (20 Hz) y el RTT que mide el panel es real.
    if (delivered && ackLen > 0 && model_.applyTelemetry(ack, ackLen)) {
        PcView::sendTelemetry(model_);
    }
}

void BridgeController::tick(uint32_t nowMs)
{
    if (nowMs - lastStatusMs_ < cfg::kStatusPeriodMs) return;
    lastStatusMs_ = nowMs;
    PcView::sendStatus(model_, nowMs);
}
