#ifndef LINK_H
#define LINK_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "pinout.h"
#include "drive.h"
#include "steering.h"
#include "rover_state.h"
#include "diag.h"

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
void     webBroadcastTelemetry(const char *json, size_t len);
int      webClientCount();
uint32_t webDroppedFrames();
uint32_t webConnects();
uint32_t webDisconnects();
bool     webBroadcastLog(const char *msg);

/*
 * Log por serie sin romper el flujo JSON: siempre una linea completa con
 * prefijo "# " escrita de una sola vez. El panel descarta cualquier linea que
 * no empiece por '{'.
 */
static void logLine(const char *fmt, ...) {
  // 256 y no 160: las lineas de DIAG/SALUD rozan los 155 caracteres y se
  // truncaban justo donde estan los datos que interesan.
  char buf[256];
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

  /*
   * El mismo texto va tambien por el WebSocket. Conduciendo por WiFi el cable
   * serie no esta enchufado, y es justo entonces cuando hacen falta los logs:
   * el panel los recibe, los graba y los deja descargar como archivo.
   * buf+2 salta el prefijo "# "; el \n final se pisa con el terminador.
   */
  buf[len - 1] = '\0';
  webBroadcastLog(buf + 2);
}

static void handleCommand(JsonDocument &doc) {
  const char *act = doc["action"];
  if (!act) {
    g_state.rxErr++;
    return;
  }

  // Cualquier trama valida cuenta como senal de vida para el failsafe.
  // El hueco se mide ANTES de renovar: es el dato que dice si el flujo llega
  // parejo a 20 Hz o a rafagas con silencios largos.
  uint32_t hueco = millis() - g_state.lastPacketMs;
  diagMax(g_gap_max_ms, hueco);
  stateMarkPacket();
  g_state.rxOk++;
  g_lines_ok++;
  diagSaveAct(act);
  if (doc["seq"].is<uint32_t>()) g_state.lastSeq = doc["seq"].as<uint32_t>();

  if (strcmp(act, "HEARTBEAT") == 0) {
    diagEv(D_HB, (int32_t)hueco);
    return;                                  // ya renovo el failsafe
  }

  // Parar es universal: no pasa por el arbitraje del mando y ademas lo libera.
  if (strcmp(act, "DISARM") == 0 || strcmp(act, "STOP") == 0) {
    g_state.armed = false;
    roverSafeStop();
    stateReleaseControl();
    diagEv(strcmp(act, "STOP") == 0 ? D_STOP : D_DISARM, (int32_t)hueco);
    logLine("ROVER DESARMADO.");
    return;
  }

  // A partir de aqui todo son ordenes que mueven hardware: exigen el mando.
  const char *client = doc["client"];
  if (!stateClaimControl(client)) {
    /*
     * Otro operador esta al mando. Ojo: la trama YA renovo el failsafe arriba,
     * asi que esto no corta nada, pero si el panel perdiera su id de cliente a
     * mitad de sesion acabaria aqui y sus DRIVE dejarian de mover el rover sin
     * ningun aviso. Por eso se cuenta.
     */
    g_lines_noctrl++;
    diagEv(D_NOCTRL, (int32_t)g_lines_noctrl);
    return;
  }

  if (strcmp(act, "ARM") == 0) {
    g_state.armed = true;
    diagEv(D_ARM, (int32_t)hueco);
    logLine("ROVER ARMADO.");
    return;
  }

  if (strcmp(act, "DRIVE") == 0) {
    if (!g_state.armed) return;              // se ignora en silencio si esta desarmado
    if (!doc["linear"].is<float>() || !doc["angular"].is<float>()) return;
    float lin = doc["linear"].as<float>();
    float ang = doc["angular"].as<float>();
    driveSetPercent(lin);
    steeringSetAngular(ang);
    // val = hueco desde la trama anterior; es lo que delata un flujo irregular.
    diagEv(D_DRIVE, (int32_t)hueco);
    return;
  }

  if (strcmp(act, "TEST_MOTOR") == 0) {
    // Con los 6 motores en paralelo el indice "motor" no distingue nada:
    // cualquier prueba mueve el canal unico de traccion.
    if (doc["speed"].is<float>()) {
      driveSetPercent(doc["speed"].as<float>());
      diagEv(D_TEST, (int32_t)doc["speed"].as<float>());
    }
    return;
  }

  if (strcmp(act, "SERVO_SET") == 0) {
    if (doc["channel"].is<int>() && doc["angle"].is<float>()) {
      int ch = doc["channel"].as<int>();
      if (ch >= 0 && ch < STEER_SERVO_COUNT) {
        steeringSetChannelAngle((uint8_t)ch, doc["angle"].as<float>());
        diagEv(D_SERVO, ch);
      }
    }
    return;
  }

  logLine("Accion desconocida: %s", act);
}

// Parsea una linea completa y la despacha.
void linkDispatchLine(const char *line) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line, MAX_LINE_LEN);
  if (err != DeserializationError::Ok) {
    g_state.rxErr++;
    // La linea cruda es lo mas util que hay cuando el parseo falla: dice si
    // llego truncada, partida a la mitad o con dos ordenes pegadas.
    diagSaveBad(line);
    diagEv(D_RXBAD, (int32_t)strlen(line));
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

/*
 * Emite telemetria solo cuando el estado cambia (armado, mando, traccion,
 * servos, clientes o errores); si nada cambia, un latido cada
 * ROVER_TELEMETRY_KEEPALIVE_MS mantiene vivo el enlace sin saturar el puerto.
 * uptime, heap y rx_ok se excluyen a proposito de la comparacion: cambian
 * siempre y volverian el filtro inutil.
 */
void linkTelemetryTick() {
  struct Snapshot {
    bool     armed;
    char     owner[OWNER_ID_LEN];
    float    drive;
    float    servos[STEER_SERVO_COUNT];
    int      clients;
    uint32_t rxErr;
  };
  static Snapshot last = {};
  static uint32_t lastTxMs = 0;

  Snapshot now = {};   // = {} tambien pone a cero el padding: memcmp lo compara
  now.armed = g_state.armed;
  memcpy(now.owner, g_state.owner, sizeof(now.owner));
  now.drive = driveGetPercent();
  for (uint8_t ch = 0; ch < STEER_SERVO_COUNT; ch++) now.servos[ch] = steeringGetAngle(ch);
  now.clients = webClientCount();
  now.rxErr = g_state.rxErr;

  bool changed = memcmp(&now, &last, sizeof(now)) != 0;
  if (!changed && millis() - lastTxMs < ROVER_TELEMETRY_KEEPALIVE_MS) return;
  last = now;
  lastTxMs = millis();

  char buf[TELEMETRY_BUF_LEN];
  int n = linkBuildTelemetry(buf, sizeof(buf));
  if (n <= 0) return;
  if ((size_t)n >= sizeof(buf)) n = sizeof(buf) - 1;

  uint32_t t0 = micros();
  Serial.write((const uint8_t *)buf, n);     // una sola escritura: sin lineas entrelazadas
  webBroadcastTelemetry(buf, n);
  uint32_t dt = micros() - t0;
  diagMax(g_tlm_max_us, dt);
  if (dt > DIAG_STALL_US) diagEv(D_TLMSLOW, (int32_t)(dt / 1000));
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


/* ------------------------------------------------------------------------ */
/* Volcado de la caja negra                                                  */
/* ------------------------------------------------------------------------ */

/*
 * Imprime el anillo y el resumen de estado. Solo se llama cuando algo va mal
 * (failsafe) o a peticion, nunca en el lazo de 20 Hz.
 *
 * Cada evento sale como "-N:NOMBRE(v)", donde N son los milisegundos HACIA
 * ATRAS desde el instante del volcado. Asi se lee de un vistazo el orden de lo
 * que paso justo antes del corte.
 */
void diagDump(const char *motivo) {
#if ROVER_DIAG
  uint32_t ahora = millis();
  uint32_t n = (g_diag_total < DIAG_RING) ? g_diag_total : DIAG_RING;
  uint8_t  idx = (uint8_t)((g_diag_head + DIAG_RING - n) % DIAG_RING);

  logLine("===== DIAG (%s) t=%lu ms =====", motivo, (unsigned long)ahora);

  char linea[152];
  size_t p = 0;
  for (uint32_t i = 0; i < n; i++) {
    const DiagEv &e = g_diag[idx];
    idx = (uint8_t)((idx + 1) % DIAG_RING);

    char item[40];
    int w = snprintf(item, sizeof(item), "-%lu:%s(%ld) ",
                     (unsigned long)(ahora - e.ms),
                     (e.code < (sizeof(DIAG_NAME) / sizeof(DIAG_NAME[0]))) ? DIAG_NAME[e.code] : "?",
                     (long)e.val);
    if (w <= 0) continue;
    if (p + (size_t)w >= sizeof(linea)) {          // no cabe: cierra la linea
      linea[p] = '\0';
      logLine("%s", linea);
      p = 0;
    }
    memcpy(linea + p, item, (size_t)w);
    p += (size_t)w;
  }
  if (p > 0) { linea[p] = '\0'; logLine("%s", linea); }

  /*
   * El renglon decisivo. Comparar los dos silencios dice donde mirar:
   *   ws alto  + trama alto -> no llegan bytes: WiFi o cliente caido
   *   ws bajo  + trama alto -> llegan y se rechazan: mirar rx_err y linea_mala
   */
  logLine("DIAG silencio_trama=%lu ms  silencio_ws=%lu ms  clientes=%d  armado=%d  traccion=%.1f%%",
          (unsigned long)(ahora - g_state.lastPacketMs),
          (unsigned long)(ahora - g_ws_last_data_ms),
          webClientCount(), g_state.armed ? 1 : 0, driveGetPercent());
  logLine("DIAG rx_ok=%lu rx_err=%lu lineas_ok=%lu sin_mando=%lu descartadas=%lu ws+=%lu ws-=%lu bytes_ws=%lu",
          (unsigned long)g_state.rxOk, (unsigned long)g_state.rxErr,
          (unsigned long)g_lines_ok, (unsigned long)g_lines_noctrl,
          (unsigned long)webDroppedFrames(),
          (unsigned long)webConnects(), (unsigned long)webDisconnects(),
          (unsigned long)g_ws_bytes);
  logLine("DIAG hueco_max=%lu ms  loop_max=%lu us  i2c_max=%lu us  tlm_max=%lu us  heap=%lu  pca=%d",
          (unsigned long)g_gap_max_ms, (unsigned long)g_loop_max_us,
          (unsigned long)g_i2c_max_us, (unsigned long)g_tlm_max_us,
          (unsigned long)ESP.getFreeHeap(), steeringOk() ? 1 : 0);
  logLine("DIAG ultima_accion='%s'  duenio='%s'  ultima_linea_mala='%s'",
          g_last_act, g_state.owner, g_last_bad);
  logLine("===== fin DIAG =====");

  // Los maximos se reinician: interesa el pico de CADA tramo, no el historico.
  g_gap_max_ms = g_loop_max_us = g_i2c_max_us = g_tlm_max_us = 0;
#else
  (void)motivo;
#endif
}

/*
 * Linea de salud cada segundo. Es lo que permite ver el enlace SANO justo
 * antes del corte, no solo la foto del desastre.
 */
void diagHealthTick() {
#if ROVER_DIAG
  static uint32_t ult_lineas = 0, ult_bytes = 0;
  uint32_t lineas = g_lines_ok, bytes = g_ws_bytes;
  uint32_t ahora = millis();

  logLine("SALUD tramas/s=%lu bytes_ws/s=%lu hueco_max=%lu ms silencio_ws=%lu ms "
          "clientes=%d armado=%d loop_max=%lu us i2c_max=%lu us heap=%lu rx_err=%lu descartadas=%lu",
          (unsigned long)(lineas - ult_lineas), (unsigned long)(bytes - ult_bytes),
          (unsigned long)g_gap_max_ms, (unsigned long)(ahora - g_ws_last_data_ms),
          webClientCount(), g_state.armed ? 1 : 0,
          (unsigned long)g_loop_max_us, (unsigned long)g_i2c_max_us,
          (unsigned long)ESP.getFreeHeap(),
          (unsigned long)g_state.rxErr, (unsigned long)webDroppedFrames());

  ult_lineas = lineas; ult_bytes = bytes;
  g_gap_max_ms = g_loop_max_us = g_i2c_max_us = 0;
#endif
}

/* ------------------------------------------------------------------------ */
/* Vigilancia del enlace                                                     */
/* ------------------------------------------------------------------------ */

static bool     g_link_mute  = false;   // episodio de silencio en curso
static uint32_t g_mute_since = 0;       // millis() de la ultima trama antes de callar

/*
 * Vigila el silencio del enlace. Lo que hace al detectarlo depende de
 * ROVER_FAILSAFE_ENABLED (pinout.h):
 *
 *   1 -> desarma, corta la traccion y libera el mando.
 *   0 -> NO toca el hardware. Solo deja constancia: el rover mantiene la
 *        ultima consigna mientras el enlace este caido.
 *
 * La deteccion corre en los dos casos, y a proposito sin exigir que el rover
 * este armado: para juzgar si el enlace es estable interesan tambien los cortes
 * que ocurren en reposo.
 *
 * El volcado se emite DOS veces, y no es redundancia:
 *   - al enmudecer, alcanza el puerto serie (el WebSocket ya no sale),
 *   - al recuperarse, alcanza el panel, que es quien graba el archivo. El
 *     anillo conserva los eventos previos al corte, asi que el volcado tardio
 *     sigue contando lo que paso justo antes.
 */
void linkFailsafeTick() {
  uint32_t ahora   = millis();
  uint32_t silence = ahora - g_state.lastPacketMs;
  bool     mudo    = silence >= ROVER_FAILSAFE_TIMEOUT_MS;

  // Antes de la primera trama no hay nada que vigilar: recien arrancado el
  // enlace "lleva callado" todo el uptime y avisar seria ruido.
  if (g_state.rxOk == 0) return;

  if (mudo && !g_link_mute) {
    g_link_mute  = true;
    g_mute_since = g_state.lastPacketMs;
    diagEv(D_MUTE, (int32_t)silence);
#if ROVER_FAILSAFE_ENABLED
    logLine("FAILSAFE: %lu ms sin tramas, rover detenido.", (unsigned long)silence);
#else
    logLine("ENLACE MUDO: %lu ms sin tramas. Failsafe DESACTIVADO: la traccion "
            "sigue a %.1f%% y el rover permanece %s.",
            (unsigned long)silence, driveGetPercent(),
            g_state.armed ? "ARMADO" : "desarmado");
    /*
     * Se libera el mando aunque no se corte la potencia. Sin esto, un enlace
     * caido deja al rover en propiedad de un cliente fantasma: al recargar el
     * panel se genera un client id nuevo, el arbitraje lo rechaza y no hay
     * forma de retomar el control salvo mandar STOP o reiniciar. Soltar el
     * mando no mueve nada; solo permite que el operador vuelva a tomarlo.
     */
    stateReleaseControl();
#endif
    diagDump("enlace mudo");
  } else if (!mudo && g_link_mute) {
    g_link_mute = false;
    uint32_t duracion = g_state.lastPacketMs - g_mute_since;
    diagEv(D_LINKOK, (int32_t)duracion);
    logLine("ENLACE RECUPERADO tras %lu ms mudo.", (unsigned long)duracion);
    // Este si llega al panel: es el que acaba en el archivo descargable.
    diagDump("enlace recuperado");
  }

#if ROVER_FAILSAFE_ENABLED
  /*
   * El corte solo se aplica si hay hardware en movimiento: un TEST_MOTOR desde
   * diagnostico mueve las ruedas con el rover desarmado, y si el operador
   * desaparece a media prueba nadie mandaria el TEST_MOTOR de parada.
   */
  if (!mudo) return;
  if (!(g_state.armed || driveIsActive())) return;

  g_state.armed = false;
  roverSafeStop();
  stateReleaseControl();
  diagEv(D_FAILSAFE, (int32_t)silence);
#endif
}

#endif // LINK_H
