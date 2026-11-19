#include "BaseModel.h"

#include <string.h>

BaseModel::BaseModel()
    : owner_(radio::kOwnerFree), haveTelemetry_(false), pcErrors_(0), sentOk_(0),
      sentFail_(0), lastAckMs_(0), everAcked_(false), windowPos_(0), windowFill_(0),
      lastSendMs_(0), lastRetryMs_(0), retry_(0), reinits_(0)
{
    panelClient_[0] = '\0';
    memset(&rover_, 0, sizeof(rover_));
    for (int i = 0; i < proto::kServoCount; i++) rover_.servos[i] = 90.0f;
    for (int i = 0; i < cfg::kLqWindow; i++) { window_[i] = false; arcWindow_[i] = 0; }
}

void BaseModel::notePanelClient(const char *client)
{
    if (!client || client[0] == '\0') return;
    strncpy(panelClient_, client, sizeof(panelClient_) - 1);
    panelClient_[sizeof(panelClient_) - 1] = '\0';
}

void BaseModel::recordSend(bool delivered, uint8_t retries, uint32_t nowMs)
{
    lastSendMs_ = nowMs;
    if (delivered) {
        sentOk_++;
        lastAckMs_ = nowMs;
        everAcked_ = true;
        retry_ = 0;                              // enlace recuperado
    } else {
        sentFail_++;
    }

    window_[windowPos_] = delivered;
    arcWindow_[windowPos_] = retries;
    windowPos_ = (windowPos_ + 1) % cfg::kLqWindow;
    if (windowFill_ < cfg::kLqWindow) windowFill_++;
}

bool BaseModel::roverReachable(uint32_t nowMs) const
{
    return everAcked_ && (nowMs - lastAckMs_) <= cfg::kRoverLostMs;
}

int BaseModel::linkQuality() const
{
    if (windowFill_ == 0) return 0;
    int ok = 0;
    for (int i = 0; i < windowFill_; i++) ok += window_[i] ? 1 : 0;
    return (ok * 100) / windowFill_;
}

int BaseModel::avgRetriesX10() const
{
    if (windowFill_ == 0) return 0;
    int sum = 0;
    for (int i = 0; i < windowFill_; i++) sum += arcWindow_[i];
    return (sum * 10) / windowFill_;
}

bool BaseModel::reconnectDue(uint32_t nowMs) const
{
    if (!everAcked_) return false;                               // nunca se vio al rover
    if (nowMs - lastSendMs_ > cfg::kPanelActiveMs) return false;  // el panel no manda nada
    if (nowMs - lastAckMs_ <= cfg::kRoverLostMs) return false;    // el rover contesta
    uint32_t since = retry_ == 0 ? lastAckMs_ + cfg::kRoverLostMs : lastRetryMs_;
    return nowMs - since >= (retry_ == 0 ? 0 : cfg::kRetryWindowMs);
}

bool BaseModel::applyTelemetry(const uint8_t *buf, uint8_t len)
{
    radio::OwnerCode owner;
    if (radio::decodeTelemetry(buf, len, rover_, owner) == 0) return false;
    owner_ = owner;
    haveTelemetry_ = true;
    return true;
}

TelemetryData BaseModel::telemetryForPanel() const
{
    TelemetryData t = rover_;

    const char *owner = "";
    switch (owner_) {
    case radio::kOwnerRadio: owner = panelClient_[0] ? panelClient_ : radio::kRadioClientId; break;
    case radio::kOwnerOther: owner = "cable"; break;   // un panel por USB directo al rover
    default:                 owner = "";      break;
    }
    strncpy(t.owner, owner, sizeof(t.owner) - 1);
    t.owner[sizeof(t.owner) - 1] = '\0';

    // Las tramas que la base no pudo ni leer tambien son tramas con error.
    t.rxErr  += pcErrors_;
    t.clients = 1;
    t.radioLq = linkQuality();
    return t;
}
