#pragma once

#include "Command.h"

/*
 * Decodificador del protocolo de mando: una linea JSON plana por orden.
 *
 *   {"action":"DRIVE","linear":50,"angular":-10,"seq":12,"client":"pab12cd34"}
 *
 * Es el unico sitio que conoce el formato de cable. Parser propio y sin malloc
 * (en vez de cJSON/ArduinoJson) porque el protocolo es un objeto plano de
 * cadenas y numeros: asi el sketch compila sin instalar librerias.
 * Los valores anidados se aceptan y se ignoran.
 */
namespace JsonProtocol {

/**
 * @return false si la linea no es un objeto JSON valido o no trae "action"
 *         como cadena (trama invalida, cuenta en rx_err).
 */
bool parse(const char *line, Command &out);

}  // namespace JsonProtocol
