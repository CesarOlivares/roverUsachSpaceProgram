#pragma once

#include <string.h>
#include "../common/JsonProtocol.h"
#include "../controller/CommandController.h"

/*
 * Ordenes de configuracion del Wi-Fi ({"action":"WIFI_..."}). No son de mando
 * ni forman parte del protocolo compartido con la base: las atiende WifiLink,
 * llegue la linea por cable o por WebSocket. Por radio no pasan (la base no
 * las conoce). Devuelve true si la linea era suya.
 */
using LineHook = bool (*)(const char *line);
inline LineHook &wifiLineHook()
{
    static LineHook hook = nullptr;
    return hook;
}

// Decodifica una linea JSON completa y la entrega al controlador.
inline void dispatchLine(CommandController &ctrl, const char *line)
{
    LineHook hook = wifiLineHook();
    if (hook && strstr(line, "\"WIFI_") && hook(line)) return;

    Command cmd;
    if (JsonProtocol::parse(line, cmd)) {
        ctrl.handle(cmd);
    } else {
        ctrl.reportInvalidFrame();
    }
}
