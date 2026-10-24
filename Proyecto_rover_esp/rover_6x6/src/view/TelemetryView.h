#pragma once

#include <stddef.h>
#include <stdint.h>
#include "../model/RoverModel.h"

/*
 * Vista del firmware: el estado del rover tal como lo ven los paneles.
 *
 *   - Por cable: una linea JSON (el formato de siempre).
 *   - Por radio: paquetes binarios que viajan en el ACK; la estacion base los
 *     vuelve a convertir en la misma linea JSON para el panel.
 *
 * Todos los campos son medidas reales. No hay lectura de bateria porque no hay
 * bateria instrumentada: el divisor del GPIO 9 no esta montado.
 */
namespace TelemetryView {

/** Linea JSON terminada en '\n'. @return bytes escritos, 0 si no cabe. */
size_t renderJson(const RoverSnapshot &s, char *out, size_t outLen);

/**
 * Paquete de telemetria para el ACK de radio. Alterna entre el de movimiento y
 * el de estado en cada llamada a traves de @p motion.
 * @return bytes escritos, 0 si no cabe.
 */
size_t renderRadio(const RoverSnapshot &s, bool motion, uint8_t *out, size_t outLen);

}  // namespace TelemetryView
