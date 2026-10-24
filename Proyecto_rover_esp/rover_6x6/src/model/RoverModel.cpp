#include "RoverModel.h"

#include <string.h>

RoverModel::RoverModel()
    : armed_(false), lastControlUs_(0), rxOk_(0), rxErr_(0), lastSeq_(0)
{
    owner_[0] = '\0';
    for (int i = 0; i < cfg::kMotorCount; i++) motors_[i] = 0.0f;
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

void RoverModel::setMotor(int idx, float speed)
{
    if (idx >= 0 && idx < cfg::kMotorCount) motors_[idx] = speed;
}

bool RoverModel::anyMotorActive() const
{
    for (int i = 0; i < cfg::kMotorCount; i++) {
        if (motors_[i] != 0.0f) return true;
    }
    return false;
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
    memcpy(out.motors, motors_, sizeof(out.motors));
    memcpy(out.servos, servos_, sizeof(out.servos));
    out.rxOk  = rxOk_;
    out.rxErr = rxErr_;
}
