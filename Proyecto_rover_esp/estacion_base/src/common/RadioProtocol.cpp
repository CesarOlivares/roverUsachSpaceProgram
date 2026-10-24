// COPIA GENERADA de Proyecto_rover_esp/common/ - NO EDITAR AQUI.
// Edita el original y ejecuta tools/sync_common.ps1.

#include "RadioProtocol.h"

#include <math.h>
#include <string.h>

namespace {

/* Escritor y lector little-endian con control de limites. */

class Writer {
public:
    Writer(uint8_t *buf, size_t cap) : buf_(buf), cap_(cap) {}

    void u8(uint8_t v)   { if (len_ + 1 <= cap_) buf_[len_] = v; len_ += 1; }
    void u16(uint16_t v) { u8(v & 0xFF); u8(v >> 8); }
    void u32(uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); }
    void i16(int16_t v)  { u16((uint16_t)v); }
    void i32(int32_t v)  { u32((uint32_t)v); }

    // Lo que no cabe da 0: nunca se manda un paquete truncado.
    size_t finish() const { return len_ <= cap_ ? len_ : 0; }

private:
    uint8_t *buf_;
    size_t   cap_;
    size_t   len_ = 0;
};

class Reader {
public:
    Reader(const uint8_t *buf, size_t len) : buf_(buf), len_(len) {}

    uint8_t u8() {
        if (pos_ >= len_) { ok_ = false; return 0; }
        return buf_[pos_++];
    }
    uint16_t u16() { uint16_t lo = u8(); return (uint16_t)(lo | ((uint16_t)u8() << 8)); }
    uint32_t u32() { uint32_t lo = u16(); return lo | ((uint32_t)u16() << 16); }
    int16_t  i16() { return (int16_t)u16(); }
    int32_t  i32() { return (int32_t)u32(); }

    bool ok() const { return ok_; }

private:
    const uint8_t *buf_;
    size_t         len_;
    size_t         pos_ = 0;
    bool           ok_  = true;
};

// Decimas con saturacion a int16: -3276.8 .. 3276.7.
int16_t toTenths(float v)
{
    if (isnan(v)) return 0;
    float t = roundf(v * 10.0f);
    if (t >  32767.0f) return  32767;
    if (t < -32768.0f) return -32768;
    return (int16_t)t;
}

int16_t clampI16(int32_t v)
{
    if (v >  32767) return  32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

bool knownCommand(uint8_t t)
{
    return t <= (uint8_t)CommandType::StepperJog;
}

}  // namespace

namespace radio {

OwnerCode ownerCodeOf(const char *owner)
{
    if (!owner || owner[0] == '\0') return kOwnerFree;
    if (strcmp(owner, kRadioClientId) == 0) return kOwnerRadio;
    return kOwnerOther;
}

/* ------------------------------------------------------------------------ */
/* Orden: [ver][0x10][tipo][flags][seq u32][argumentos]                      */
/*   flags: bit0 hasSeq, bit1 argsOk                                        */
/* ------------------------------------------------------------------------ */

size_t encodeCommand(const Command &cmd, uint8_t *out, size_t outLen)
{
    if (!knownCommand((uint8_t)cmd.type)) return 0;

    Writer w(out, outLen < kPayloadMax ? outLen : kPayloadMax);
    w.u8(kVersion);
    w.u8(kPktCommand);
    w.u8((uint8_t)cmd.type);
    w.u8((uint8_t)((cmd.hasSeq ? 0x01 : 0) | (cmd.argsOk ? 0x02 : 0)));
    w.u32(cmd.seq);

    switch (cmd.type) {
    case CommandType::Drive:
        w.i16(toTenths(cmd.linear));
        w.i16(toTenths(cmd.angular));
        break;
    case CommandType::TestMotor:
        w.i16(clampI16(cmd.motor));
        w.i16(toTenths(cmd.speed));
        break;
    case CommandType::ServoSet:
        w.i16(clampI16(cmd.channel));
        w.i16(toTenths(cmd.angle));
        break;
    case CommandType::StepperJog:
        w.i32(cmd.steps);
        w.u16(cmd.stepHz > 0xFFFF ? 0xFFFF : (uint16_t)cmd.stepHz);
        break;
    default:
        break;                                   // ordenes sin argumentos
    }
    return w.finish();
}

bool decodeCommand(const uint8_t *in, size_t len, Command &out)
{
    Reader r(in, len);
    if (r.u8() != kVersion || r.u8() != kPktCommand) return false;

    uint8_t type  = r.u8();
    uint8_t flags = r.u8();
    uint32_t seq  = r.u32();
    if (!r.ok() || !knownCommand(type)) return false;

    out = Command();
    out.type   = (CommandType)type;
    out.hasSeq = flags & 0x01;
    out.argsOk = flags & 0x02;
    out.seq    = seq;
    strncpy(out.client, kRadioClientId, sizeof(out.client) - 1);

    switch (out.type) {
    case CommandType::Drive:
        out.linear  = r.i16() / 10.0f;
        out.angular = r.i16() / 10.0f;
        break;
    case CommandType::TestMotor:
        out.motor = r.i16();
        out.speed = r.i16() / 10.0f;
        break;
    case CommandType::ServoSet:
        out.channel = r.i16();
        out.angle   = r.i16() / 10.0f;
        break;
    case CommandType::StepperJog:
        out.steps  = r.i32();
        out.stepHz = r.u16();
        break;
    default:
        break;
    }
    return r.ok();
}

/* ------------------------------------------------------------------------ */
/* Telemetria                                                                */
/*   comun:      [ver][tipo][flags: bit0 armado, bit1 paso a paso, bit2-3 owner] */
/*   movimiento: [seq u32][motores i16 x6 (decimas)][servos u8 x5]   = 24 B   */
/*   estado:     [uptime u32][pos i32][rx_ok u32][rx_err u32][heap u32] = 23 B */
/* ------------------------------------------------------------------------ */

size_t encodeTelemetry(const TelemetryData &t, bool motion, uint8_t *out, size_t outLen)
{
    Writer w(out, outLen < kPayloadMax ? outLen : kPayloadMax);
    w.u8(kVersion);
    w.u8(motion ? kPktTelemetryMotion : kPktTelemetryStatus);
    w.u8((uint8_t)((t.armed ? 0x01 : 0) |
                   (t.stepperMoving ? 0x02 : 0) |
                   ((ownerCodeOf(t.owner) & 0x03) << 2)));

    if (motion) {
        w.u32(t.seq);
        for (int i = 0; i < proto::kMotorCount; i++) w.i16(toTenths(t.motors[i]));
        for (int i = 0; i < proto::kServoCount; i++) {
            float a = isnan(t.servos[i]) ? 0.0f : t.servos[i];
            if (a < 0.0f)   a = 0.0f;
            if (a > 180.0f) a = 180.0f;
            w.u8((uint8_t)(a + 0.5f));
        }
    } else {
        w.u32(t.uptimeS);
        w.i32(t.stepperPos);
        w.u32(t.rxOk);
        w.u32(t.rxErr);
        w.u32(t.freeHeap);
    }
    return w.finish();
}

uint8_t decodeTelemetry(const uint8_t *in, size_t len, TelemetryData &t, OwnerCode &owner)
{
    Reader r(in, len);
    if (r.u8() != kVersion) return 0;
    uint8_t type  = r.u8();
    uint8_t flags = r.u8();
    if (!r.ok()) return 0;

    if (type == kPktTelemetryMotion) {
        TelemetryData tmp = t;
        tmp.seq = r.u32();
        for (int i = 0; i < proto::kMotorCount; i++) tmp.motors[i] = r.i16() / 10.0f;
        for (int i = 0; i < proto::kServoCount; i++) tmp.servos[i] = r.u8();
        if (!r.ok()) return 0;
        t = tmp;
    } else if (type == kPktTelemetryStatus) {
        TelemetryData tmp = t;
        tmp.uptimeS    = r.u32();
        tmp.stepperPos = r.i32();
        tmp.rxOk       = r.u32();
        tmp.rxErr      = r.u32();
        tmp.freeHeap   = r.u32();
        if (!r.ok()) return 0;
        t = tmp;
    } else {
        return 0;
    }

    t.armed         = flags & 0x01;
    t.stepperMoving = flags & 0x02;
    owner           = (OwnerCode)((flags >> 2) & 0x03);
    return type;
}

}  // namespace radio
