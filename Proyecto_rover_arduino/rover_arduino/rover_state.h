#ifndef ROVER_STATE_H
#define ROVER_STATE_H

#include <Arduino.h>
#include "drive.h"
#include "steering.h"

/*
 * Estado del enlace y arbitraje de mando exclusivo (port de rover_link.c).
 *
 * Puede haber varios paneles conectados a la vez emitiendo a 20 Hz. Sin
 * arbitraje, dos clientes armados mandarian DRIVE contradictorios en el mismo
 * ciclo. El primero que actua se queda el mando; el resto solo ve telemetria.
 * Se libera con DISARM, con STOP o cuando salta el failsafe. Parar siempre lo
 * puede cualquiera.
 *
 * A diferencia del firmware ESP-IDF aqui no hacen falta locks: los callbacks
 * del servidor asincrono y el loop() de Arduino corren en el mismo nucleo.
 */

#define OWNER_ID_LEN 24

struct RoverState {
  bool     armed        = false;
  char     owner[OWNER_ID_LEN] = {0};   // cadena vacia = mando libre
  uint32_t rxOk         = 0;            // tramas JSON aceptadas
  uint32_t rxErr        = 0;            // lineas descartadas
  uint32_t lastSeq      = 0;            // secuencia del cliente, se devuelve para medir RTT
  uint32_t lastPacketMs = 0;            // millis() de la ultima trama valida (failsafe)
};

static RoverState g_state;

static inline void stateMarkPacket() {
  g_state.lastPacketMs = millis();
}

/*
 * Concede el mando a @p client si esta libre o ya era suyo. El id se copia
 * saneado (solo alfanumericos): viene de la red y se reenvia dentro de la
 * telemetria JSON, una comilla romperia la trama de todos los clientes.
 * Un cliente anonimo (NULL) actua si nadie tiene el mando, pero no lo reclama.
 */
static bool stateClaimControl(const char *client) {
  char id[OWNER_ID_LEN] = {0};
  if (client) {
    size_t n = 0;
    for (const char *p = client; *p && n < sizeof(id) - 1; p++) {
      if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9')) {
        id[n++] = *p;
      }
    }
  }

  if (g_state.owner[0] == '\0') {
    memcpy(g_state.owner, id, sizeof(g_state.owner));
    return true;
  }
  return (id[0] != '\0') && (strcmp(g_state.owner, id) == 0);
}

static inline void stateReleaseControl() {
  g_state.owner[0] = '\0';
}

// Corta toda la potencia y centra la direccion.
static void roverSafeStop() {
  driveStop();
  steeringCenterAll();
}

#endif // ROVER_STATE_H
