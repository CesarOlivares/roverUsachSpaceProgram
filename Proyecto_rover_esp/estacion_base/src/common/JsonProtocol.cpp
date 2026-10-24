// COPIA GENERADA de Proyecto_rover_esp/common/ - NO EDITAR AQUI.
// Edita el original y ejecuta tools/sync_common.ps1.

#include "JsonProtocol.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace {

/* ------------------------------------------------------------------------ */
/* Lexico minimo de JSON                                                    */
/* ------------------------------------------------------------------------ */

void skipWs(const char *&p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
}

bool isHex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Lee una cadena JSON. p apunta a la comilla de apertura. Si out es nullptr
// solo la salta. Lo que no cabe en out se descarta sin error.
bool readString(const char *&p, char *out, size_t outLen)
{
    if (*p != '"') return false;
    p++;

    size_t n = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            char e = *p++;
            switch (e) {
            case '"':  c = '"';  break;
            case '\\': c = '\\'; break;
            case '/':  c = '/';  break;
            case 'b':  c = '\b'; break;
            case 'f':  c = '\f'; break;
            case 'n':  c = '\n'; break;
            case 'r':  c = '\r'; break;
            case 't':  c = '\t'; break;
            case 'u':
                for (int i = 0; i < 4; i++) {
                    if (!isHex(*p)) return false;
                    p++;
                }
                c = '?';                         // el protocolo no usa Unicode
                break;
            default:
                return false;
            }
        } else if ((unsigned char)c < 0x20) {
            return false;                        // control sin escapar: JSON invalido
        }
        if (out && n + 1 < outLen) out[n++] = c;
    }
    if (*p != '"') return false;
    p++;
    if (out && outLen > 0) out[n] = '\0';
    return true;
}

bool readNumber(const char *&p, double &v)
{
    if (!(*p == '-' || (*p >= '0' && *p <= '9'))) return false;
    char *end = nullptr;
    v = strtod(p, &end);
    if (end == p || !isfinite(v)) return false;
    p = end;
    return true;
}

bool skipLiteral(const char *&p, const char *lit)
{
    size_t n = strlen(lit);
    if (strncmp(p, lit, n) != 0) return false;
    p += n;
    return true;
}

// Salta un valor cualquiera, incluidos objetos y arreglos anidados.
bool skipValue(const char *&p)
{
    if (*p == '"') return readString(p, nullptr, 0);

    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (*p) {
            if (*p == '"') {
                if (!readString(p, nullptr, 0)) return false;
                continue;
            }
            if (*p == '{' || *p == '[') {
                depth++;
            } else if (*p == '}' || *p == ']') {
                if (--depth == 0) { p++; return true; }
            }
            p++;
        }
        return false;
    }

    if (skipLiteral(p, "true") || skipLiteral(p, "false") || skipLiteral(p, "null")) return true;

    double ignored;
    return readNumber(p, ignored);
}

/* ------------------------------------------------------------------------ */
/* Campos del protocolo                                                     */
/* ------------------------------------------------------------------------ */

enum NumField { F_SEQ, F_LINEAR, F_ANGULAR, F_MOTOR, F_SPEED, F_CHANNEL, F_ANGLE, F_STEPS, F_COUNT };

const char *const kNumKeys[F_COUNT] = {
    "seq", "linear", "angular", "motor", "speed", "channel", "angle", "steps",
};

struct Fields {
    char   action[16]   = {0};
    bool   hasAction    = false;
    char   client[64]   = {0};
    bool   hasClient    = false;
    double num[F_COUNT] = {0};
    bool   has[F_COUNT] = {false};
};

int numFieldIndex(const char *key)
{
    for (int i = 0; i < F_COUNT; i++) {
        if (strcmp(key, kNumKeys[i]) == 0) return i;
    }
    return -1;
}

bool parseObject(const char *p, Fields &f)
{
    skipWs(p);
    if (*p != '{') return false;
    p++;
    skipWs(p);
    if (*p == '}') return true;

    while (true) {
        char key[16];
        skipWs(p);
        if (!readString(p, key, sizeof(key))) return false;
        skipWs(p);
        if (*p != ':') return false;
        p++;
        skipWs(p);

        int idx;
        if (strcmp(key, "action") == 0 && *p == '"') {
            if (!readString(p, f.action, sizeof(f.action))) return false;
            f.hasAction = true;
        } else if (strcmp(key, "client") == 0 && *p == '"') {
            if (!readString(p, f.client, sizeof(f.client))) return false;
            f.hasClient = true;
        } else if ((idx = numFieldIndex(key)) >= 0 && (*p == '-' || (*p >= '0' && *p <= '9'))) {
            if (!readNumber(p, f.num[idx])) return false;
            f.has[idx] = true;
        } else {
            // Clave desconocida o tipo inesperado: se ignora, como hacia cJSON
            // al pedir un numero y encontrar otra cosa.
            if (!skipValue(p)) return false;
        }

        skipWs(p);
        if (*p == ',') { p++; continue; }
        if (*p == '}') return true;
        return false;
    }
}

// Conversion con saturacion, como valueint de cJSON.
int32_t toInt32(double v)
{
    if (v >= (double)INT32_MAX) return INT32_MAX;
    if (v <= (double)INT32_MIN) return INT32_MIN;
    return (int32_t)v;
}

uint32_t toUint32(double v)
{
    if (v <= 0.0) return 0;
    if (v >= (double)UINT32_MAX) return UINT32_MAX;
    return (uint32_t)v;
}

CommandType actionType(const char *a)
{
    if (strcmp(a, "HEARTBEAT")   == 0) return CommandType::Heartbeat;
    if (strcmp(a, "ARM")         == 0) return CommandType::Arm;
    if (strcmp(a, "DISARM")      == 0) return CommandType::Disarm;
    if (strcmp(a, "STOP")        == 0) return CommandType::Stop;
    if (strcmp(a, "DRIVE")       == 0) return CommandType::Drive;
    if (strcmp(a, "TEST_MOTOR")  == 0) return CommandType::TestMotor;
    if (strcmp(a, "SERVO_SET")   == 0) return CommandType::ServoSet;
    if (strcmp(a, "STEPPER_JOG") == 0) return CommandType::StepperJog;
    return CommandType::Unknown;
}

/*
 * El id viene de la red y se reenvia dentro de la telemetria JSON: una comilla
 * o una barra romperia la trama de todos los clientes. Solo alfanumericos.
 */
void sanitizeClient(const char *in, char *out, size_t outLen)
{
    size_t n = 0;
    for (const char *c = in; *c && n + 1 < outLen; c++) {
        if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9')) {
            out[n++] = *c;
        }
    }
    out[n] = '\0';
}

}  // namespace

namespace JsonProtocol {

bool parse(const char *line, Command &out)
{
    Fields f;
    if (!line || !parseObject(line, f) || !f.hasAction) return false;

    out = Command();
    out.type = actionType(f.action);

    if (f.hasClient) sanitizeClient(f.client, out.client, sizeof(out.client));

    if (f.has[F_SEQ]) {
        out.hasSeq = true;
        out.seq    = toUint32(f.num[F_SEQ]);
    }

    switch (out.type) {
    case CommandType::Drive:
        out.argsOk  = f.has[F_LINEAR] && f.has[F_ANGULAR];
        out.linear  = (float)f.num[F_LINEAR];
        out.angular = (float)f.num[F_ANGULAR];
        break;

    case CommandType::TestMotor:
        out.argsOk = f.has[F_MOTOR] && f.has[F_SPEED];
        out.motor  = toInt32(f.num[F_MOTOR]);
        out.speed  = (float)f.num[F_SPEED];
        break;

    case CommandType::ServoSet:
        out.argsOk  = f.has[F_CHANNEL] && f.has[F_ANGLE];
        out.channel = toInt32(f.num[F_CHANNEL]);
        out.angle   = (float)f.num[F_ANGLE];
        break;

    case CommandType::StepperJog:
        out.argsOk = f.has[F_STEPS];
        out.steps  = toInt32(f.num[F_STEPS]);
        out.stepHz = f.has[F_SPEED] ? toUint32(f.num[F_SPEED]) : 0;
        break;

    default:
        out.argsOk = true;                       // ordenes sin argumentos
        break;
    }
    return true;
}

}  // namespace JsonProtocol
