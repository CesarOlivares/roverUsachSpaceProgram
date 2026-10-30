#include "RoverModel.h"

#include <string.h>

RoverModel::RoverModel()
    : armed_(false), lastControlUs_(0), rxOk_(0), rxErr_(0), lastSeq_(0), traction_(0.0f)
{
    owner_[0] = '\0';
    for (int i = 0; i < cfg::kServoCount; i++) servos_[i] = cfg::kSteerCenterDeg;
}

bool RoverModel::claimControl(const char *client)
{
    const bool identified = client && client[0] != '\0';

    if (!hasOwner()) {
        if (identified) {
            strncpy(owner_, client, sizeof(owner_) - 1);
            owner_[sizeof(owner_) - 1] = '\0';
        }
        return true;
    }
    return identified && strcmp(owner_, client) == 0;
}

void RoverModel::notePacket(const char *client, bool hasSeq, uint32_t seq, int64_t nowUs)
{
    rxOk_++;
    if (hasSeq) lastSeq_ = seq;

    /*
     * Antes cualquier trama renovaba el failsafe. Un panel observador late a
     * 20 Hz, asi que si el operador perdia el enlace con el rover armado, el
     * observador lo mantenia andando con la ultima consigna de forma indefinida.
     */
    const bool fromOwner = client && client[0] != '\0' && strcmp(owner_, client) == 0;
    if (!hasOwner() || fromOwner) {
        lastControlUs_ = nowUs;
    }
}

void RoverModel::setServo(int ch, float angle)
{
    if (ch >= 0 && ch < cfg::kServoCount) servos_[ch] = angle;
}

void RoverModel::fillSnapshot(RoverSnapshot &out) const
{
    out.seq   = lastSeq_;
    out.armed = armed_;
    memcpy(out.owner, owner_, sizeof(out.owner));
    // La telemetria conserva las 6 ruedas: todas van a la misma consigna
    // porque las mueve el mismo puente H.
    for (int i = 0; i < cfg::kWheelCount; i++) out.motors[i] = traction_;
    memcpy(out.servos, servos_, sizeof(out.servos));
    out.rxOk  = rxOk_;
    out.rxErr = rxErr_;
}
