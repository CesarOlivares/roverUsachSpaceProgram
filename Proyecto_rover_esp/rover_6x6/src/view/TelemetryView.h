#pragma once

#include <stddef.h>
#include "../model/RoverModel.h"

/*
 * Vista del firmware: el estado del rover tal como lo ven los paneles.
 *
 * Serializa con snprintf sobre un buffer del llamador: sin malloc en el lazo
 * de 20 Hz. Todos los campos son medidas reales. No hay lectura de bateria
 * porque no hay bateria instrumentada: el divisor del GPIO 9 no esta montado y
 * publicar el ruido de un pin al aire como "voltaje" seria un dato inventado.
 */
namespace TelemetryView {

/**
 * Escribe una linea JSON terminada en '\n'.
 * @param clients clientes WebSocket vivos (0 si el addon web esta apagado)
 * @return bytes escritos, siempre < outLen; 0 si no cabe.
 */
size_t render(const RoverSnapshot &s, int clients, char *out, size_t outLen);

}  // namespace TelemetryView
