#ifndef LINK_H
#define LINK_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "pinout.h"
#include "drive.h"
#include "steering.h"
#include "rover_state.h"

/*
 * Protocolo del enlace: una orden JSON por linea, terminada en '\n'. El mismo
 * dispatcher atiende al WebSocket y al puerto serie USB (el panel puede
 * conectarse por Web Serial ademas de por WiFi).
 *
 * Comandos: HEARTBEAT, ARM, DISARM, STOP, DRIVE{linear,angular},
 * TEST_MOTOR{motor,speed} (el indice se ignora: hay un solo canal de traccion),
 * SERVO_SET{channel,angle}.
 */

#define MAX_LINE_LEN      256
#define TELEMETRY_BUF_LEN 384

// Implementadas en web_server.h; declaradas aqui para no incluirlo en circulo.
void webBroadcastTelemetry(const char *json, size_t len);
int  webClientCount();

/*
 * Log por serie sin romper el flujo JSON: siempre una linea completa con
 * prefijo "# " escrita de una sola vez. El panel descarta cualquier linea que
 * no empiece por '{'.
 */
static void logLine(const char *fmt, ...) {
  char buf[160];
  buf[0] = '#'; buf[1] = ' ';
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf + 2, sizeof(buf) - 3, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  // vsnprintf escribe como mucho sizeof(buf)-4 caracteres de contenido (+NUL).
  size_t len = 2 + min((size_t)n, sizeof(buf) - 4);
  buf[len++] = '\n';
  Serial.write((const uint8_t *)buf, len);
}

static void handleCommand(JsonDocument &doc) {
  const char *act = doc["action"];
  if (!act) {
    g_state.rxErr++;
    return;
  }

  // Cualquier trama valida cuenta como senal de vida para el failsafe.
  stateMarkPacket();
  g_state.rxOk++;
  if (doc["seq"].is<uint32_t>()) g_state.lastSeq = doc["seq"].as<uint32_t>();

  if (strcmp(act, "HEARTBEAT") == 0) {
    return;                                  // ya renovo el failsafe
  }

  // Parar es universal: no pasa por el arbitraje del mando y ademas lo libera.
  if (strcmp(act, "DISARM") == 0 || strcmp(act, "STOP") == 0) {
    g_state.armed = false;
    roverSafeStop();
    stateReleaseControl();
    logLine("ROVER DESARMADO.");
    return;
  }

  // A partir de aqui todo son ordenes que mueven hardware: exigen el mando.
  const char *client = doc["client"];
  if (!stateClaimControl(client)) {
    // Otro operador esta al mando. Se descarta en silencio: a 20 Hz, avisar
    // por log inundaria la consola.
    return;
  }

  if (strcmp(act, "ARM") == 0) {
    g_state.armed = true;
    logLine("ROVER ARMADO.");
    return;
  }

  if (strcmp(act, "DRIVE") == 0) {
    if (!g_state.armed) return;              // se ignora en silencio si esta desarmado
    if (!doc["linear"].is<float>() || !doc["angular"].is<float>()) return;
    driveSetPercent(doc["linear"].as<float>());
    steeringSetAngular(doc["angular"].as<float>());
    return;
  }

  if (strcmp(act, "TEST_MOTOR") == 0) {
    // Con los 6 motores en paralelo el indice "motor" no distingue nada:
    // cualquier prueba mueve el canal unico de traccion.
    if (doc["speed"].is<float>()) driveSetPercent(doc["speed"].as<float>());
    return;
  }

  if (strcmp(act, "SERVO_SET") == 0) {
    if (doc["channel"].is<int>() && doc["angle"].is<float>()) {
      int ch = doc["channel"].as<int>();
      if (ch >= 0 && ch < STEER_SERVO_COUNT) {
        steeringSetChannelAngle((uint8_t)ch, doc["angle"].as<float>());
      }
    }
    return;
  }

  logLine("Accion desconocida: %s", act);
}

// Parsea una linea completa y la despacha.
void linkDispatchLine(const char *line) {
  JsonDocument doc;
  if (deserializeJson(doc, line, MAX_LINE_LEN) != DeserializationError::Ok) {
    g_state.rxErr++;
    return;
  }
  handleCommand(doc);
}

/*
 * Serializa el estado completo en una linea JSON. snprintf sobre buffer del
 * llamador: sin heap en el lazo de 20 Hz. "motors" lleva 6 copias de la unica
 * consigna de traccion para que el esquema de ruedas del panel siga animando.
 */
static int linkBuildTelemetry(char *out, size_t cap) {
  float m = driveGetPercent();
  return snprintf(out, cap,
      "{\"type\":\"telemetry\",\"seq\":%lu"
      ",\"uptime\":%lu"
      ",\"armed\":%s"
      ",\"owner\":\"%s\""
      ",\"motors\":[%.1f,%.1f,%.1f,%.1f,%.1f,%.1f]"
      ",\"servos\":[%.0f,%.0f,%.0f,%.0f]"
      ",\"rx_ok\":%lu,\"rx_err\":%lu"
      ",\"clients\":%d,\"heap\":%lu}\n",
      (unsigned long)g_state.lastSeq,
      (unsigned long)(millis() / 1000UL),
      g_state.armed ? "true" : "false",
      g_state.owner,
      m, m, m, m, m, m,
      steeringGetAngle(0), steeringGetAngle(1), steeringGetAngle(2), steeringGetAngle(3),
      (unsigned long)g_state.rxOk, (unsigned long)g_state.rxErr,
      webClientCount(),
      (unsigned long)ESP.getFreeHeap());
}

void linkTelemetryTick() {
  char buf[TELEMETRY_BUF_LEN];
  int n = linkBuildTelemetry(buf, sizeof(buf));
  if (n <= 0) return;
  if ((size_t)n >= sizeof(buf)) n = sizeof(buf) - 1;
  Serial.write((const uint8_t *)buf, n);     // una sola escritura: sin lineas entrelazadas
  webBroadcastTelemetry(buf, n);
}

// Lector no bloqueante del puerto serie: acumula hasta '\n' y despacha.
void linkSerialPoll() {
  static char   buf[MAX_LINE_LEN];
  static size_t len = 0;
  static bool   overflow = false;

  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n') {
      if (overflow) {
        g_state.rxErr++;
      } else if (len > 0) {
        buf[len] = '\0';
        linkDispatchLine(buf);
      }
      len = 0;
      overflow = false;
    } else if (c != '\r') {
      if (len < sizeof(buf) - 1) buf[len++] = c;
      else overflow = true;                  // linea desbordada: se tira entera
    }
  }
}

/*
 * Failsafe: vigila el enlace siempre que haya hardware en movimiento, no solo
 * cuando el rover esta armado (un TEST_MOTOR desde diagnostico tambien debe
 * pararse si el operador desaparece). Tras el corte, armed=false y traccion a
 * cero, asi que no vuelve a disparar hasta que alguien actue de nuevo.
 */
void linkFailsafeTick() {
  bool active = g_state.armed || (driveGetPercent() != 0.0f);
  if (!active) return;
  if (millis() - g_state.lastPacketMs < ROVER_FAILSAFE_TIMEOUT_MS) return;

  g_state.armed = false;
  roverSafeStop();
  stateReleaseControl();
  logLine("FAILSAFE: enlace perdido, rover detenido.");
}

#endif // LINK_H
