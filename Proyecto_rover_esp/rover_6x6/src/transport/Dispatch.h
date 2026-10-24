#pragma once

#include "JsonProtocol.h"
#include "../controller/CommandController.h"

// Decodifica una linea completa y la entrega al controlador. Comun a los dos transportes.
inline void dispatchLine(CommandController &ctrl, const char *line)
{
    Command cmd;
    if (JsonProtocol::parse(line, cmd)) {
        ctrl.handle(cmd);
    } else {
        ctrl.reportInvalidFrame();
    }
}
