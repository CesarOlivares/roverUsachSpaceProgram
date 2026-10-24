#pragma once

#include "../common/JsonProtocol.h"
#include "../controller/CommandController.h"

// Decodifica una linea JSON completa y la entrega al controlador.
inline void dispatchLine(CommandController &ctrl, const char *line)
{
    Command cmd;
    if (JsonProtocol::parse(line, cmd)) {
        ctrl.handle(cmd);
    } else {
        ctrl.reportInvalidFrame();
    }
}
