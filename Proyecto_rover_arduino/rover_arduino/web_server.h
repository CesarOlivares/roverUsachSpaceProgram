#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "pinout.h"
#include "link.h"
#include "diag.h"

/*
 * SoftAP + WebSocket de comandos (ESPAsyncWebServer).
 *
 * El panel NO se sirve desde la ESP: se abre index.html desde el computador
 * (file://) y se conecta por la opcion inalambrica a ws://192.168.4.1/ws, o
 * por cable USB. Servir 78 KB de pagina desde aqui cargaba el enlace y
 * provocaba desconexiones.
 *
 * El AP conserva las credenciales y la IP del firmware ESP-IDF (192.168.4.1,
 * la de fabrica del SoftAP).
 *
 * Los callbacks del servidor son asincronos: dentro solo se parsea JSON y se
 * escriben PWMs (microsegundos). Nada de delay() ni de esperas; la telemetria
 * sale unicamente desde loop().
 */

static AsyncWebServer g_http(80);
static AsyncWebSocket g_ws("/ws");

/*
 * Contadores de eventos WS. Se incrementan en el task asincrono y los reporta
 * webCleanupTick() desde loop(): asi el log nunca se entrelaza con la telemetria.
 */
static volatile uint32_t g_ws_connects = 0;
static volatile uint32_t g_ws_disconnects = 0;
static volatile uint32_t g_ws_dropped_frames = 0;   // tramas descartadas o desbordadas

/*
 * Ingesta del WebSocket: acumulador de linea POR CLIENTE.
 *
 * Antes se exigia que el mensaje llegase entero en un unico frame y por debajo
 * de 512 bytes; si no, se descartaba COMPLETO. El panel agrupa en un mismo
 * mensaje los eventos encolados (SERVO_SET...) mas la trama de estado
 * (HEARTBEAT/DRIVE), asi que una rafaga de 8 eventos pasaba de 512 bytes y se
 * perdia tambien el latido que iba dentro: 400 ms despues saltaba el failsafe.
 * Ese era el corte que aparecia al mover los servos por WiFi y no por cable
 * (en serie cada orden llega como una linea independiente).
 *
 * Ahora se procesan los bytes segun llegan y se despacha cada linea completa,
 * igual que hace linkSerialPoll(). Un mensaje troceado en varios segmentos TCP
 * se reensambla, y una linea excesiva solo se pierde a si misma: el resto de
 * ordenes del mismo lote siguen llegando.
 */
#define WS_LINE_MAX      256
#define WS_RX_SLOTS      4     // = clientes maximos del SoftAP

struct WsRxSlot {
  uint32_t clientId;           // 0 = libre (los id de AsyncWebSocket empiezan en 1)
  uint16_t len;
  bool     overflow;
  char     buf[WS_LINE_MAX];
};
static WsRxSlot g_ws_rx[WS_RX_SLOTS] = {};

static WsRxSlot *wsRxSlot(uint32_t id) {
  for (uint8_t i = 0; i < WS_RX_SLOTS; i++) {
    if (g_ws_rx[i].clientId == id) return &g_ws_rx[i];
  }
  for (uint8_t i = 0; i < WS_RX_SLOTS; i++) {
    if (g_ws_rx[i].clientId == 0) {
      g_ws_rx[i].clientId = id;
      g_ws_rx[i].len      = 0;
      g_ws_rx[i].overflow = false;
      return &g_ws_rx[i];
    }
  }
  return nullptr;
}

static void wsRxRelease(uint32_t id) {
  for (uint8_t i = 0; i < WS_RX_SLOTS; i++) {
    if (g_ws_rx[i].clientId == id) {
      g_ws_rx[i].clientId = 0;
      g_ws_rx[i].len      = 0;
      g_ws_rx[i].overflow = false;
    }
  }
}

// Despacha la linea acumulada en el slot y lo deja listo para la siguiente.
static void wsRxFlushLine(WsRxSlot *rx) {
  if (rx->overflow) {
    g_ws_dropped_frames++;
    g_state.rxErr++;
    diagEv(D_WSDROP, (int32_t)rx->len);
  } else if (rx->len > 0) {
    rx->buf[rx->len] = '\0';
    linkDispatchLine(rx->buf);
  }
  rx->len      = 0;
  rx->overflow = false;
}

/*
 * OJO: esto corre en la tarea de AsyncTCP, no en loop(). Aqui dentro solo se
 * parsea JSON y se apuntan consignas en RAM. Nada que bloquee: la escritura
 * I2C de los servos la hace steeringApplyTick() desde loop(), y logLine() no
 * bloquea porque setup() pone Serial.setTxTimeoutMs(0).
 */
static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  (void)server;
  if (type == WS_EVT_CONNECT) {
    g_ws_connects++;
    diagEv(D_WSCONN, (int32_t)(client ? client->id() : 0));
    return;
  }
  if (type == WS_EVT_DISCONNECT || type == WS_EVT_ERROR) {
    if (type == WS_EVT_DISCONNECT) g_ws_disconnects++;
    diagEv(type == WS_EVT_DISCONNECT ? D_WSDISC : D_WSERR,
           (int32_t)(client ? client->id() : 0));
    if (client) wsRxRelease(client->id());
    return;
  }
  if (type != WS_EVT_DATA || !client) return;

  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  /*
   * En un mensaje fragmentado los frames de continuacion llegan con opcode 0;
   * el tipo real del mensaje queda en message_opcode. Mirar solo opcode
   * descartaria la segunda mitad de un mensaje partido.
   */
  uint8_t op = info->opcode ? info->opcode : info->message_opcode;
  if (op != WS_TEXT) return;

  /*
   * Se marca ANTES de parsear: esto es "han llegado bytes por el WebSocket",
   * independiente de que la orden resulte valida. La diferencia entre este
   * sello y g_state.lastPacketMs es lo que separa un corte de transporte de un
   * rechazo de trama.
   */
  g_ws_last_data_ms = millis();
  g_ws_bytes += len;

  WsRxSlot *rx = wsRxSlot(client->id());
  if (!rx) {
    g_ws_dropped_frames++;
    diagEv(D_WSDROP, (int32_t)len);
    return;
  }

  for (size_t i = 0; i < len; i++) {
    char c = (char)data[i];
    if (c == '\n')      wsRxFlushLine(rx);
    else if (c != '\r') {
      if (rx->len < WS_LINE_MAX - 1) rx->buf[rx->len++] = c;
      else rx->overflow = true;              // linea desbordada: se tira solo esa
    }
  }

  // Fin del mensaje: despacha lo que haya quedado sin '\n' de cierre.
  if (info->final && (info->index + len) >= info->len && rx->len > 0) {
    wsRxFlushLine(rx);
  }
}

void webInit() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS, 1 /*canal*/, 0 /*visible*/, 4 /*clientes max*/);
  // Igual que el firmware ESP-IDF: sin ahorro de energia WiFi. El modem sleep
  // mete latencias a rafagas que pueden superar el failsafe de 400 ms.
  esp_wifi_set_ps(WIFI_PS_NONE);

  g_ws.onEvent(onWsEvent);
  g_http.addHandler(&g_ws);

  // Recordatorio para quien abra la IP en un navegador.
  g_http.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/plain; charset=utf-8",
              "Rover 6x6: el panel se abre desde el computador (index.html) y "
              "se conecta por Wi-Fi a esta IP, o por cable USB.");
  });

  g_http.begin();
}

void webBroadcastTelemetry(const char *json, size_t len) {
  if (g_ws.count() == 0) return;
  g_ws.textAll(json, len);
}

int webClientCount() {
  return (int)g_ws.count();
}

/*
 * Manda una linea de log a los clientes como trama JSON {"type":"log"}.
 *
 * Devuelve false si no se envio. Dos razones para no enviar:
 *   - no hay clientes,
 *   - la cola de salida del WebSocket esta llena.
 * Lo segundo importa: un volcado de diagnostico son ~7 lineas seguidas, y si
 * se encolan a la fuerza cuando no hay sitio, ESPAsyncWebServer puede cerrar
 * el cliente. Perder una linea de log es mucho mejor que tirar el enlace
 * justo mientras se diagnostica por que se cae el enlace.
 */
bool webBroadcastLog(const char *msg) {
  if (g_ws.count() == 0) return false;
  if (!g_ws.availableForWriteAll()) return false;

  char out[320];
  int p = snprintf(out, sizeof(out), "{\"type\":\"log\",\"up\":%lu,\"msg\":\"",
                   (unsigned long)millis());
  if (p < 0 || (size_t)p >= sizeof(out)) return false;

  // Escapado JSON: el texto puede traer comillas (p.ej. la ultima linea cruda
  // rechazada, que es JSON a su vez) y romperia la trama de todos los clientes.
  for (const char *c = msg; *c && (size_t)p < sizeof(out) - 8; c++) {
    if (*c == '"' || *c == '\\') { out[p++] = '\\'; out[p++] = *c; }
    else if (*c >= 32 && *c < 127)  { out[p++] = *c; }
    else                            { out[p++] = '.'; }
  }
  p += snprintf(out + p, sizeof(out) - p, "\"}\n");
  if (p < 0 || (size_t)p >= sizeof(out)) return false;

  g_ws.textAll(out, p);
  return true;
}

uint32_t webDroppedFrames() { return g_ws_dropped_frames; }
uint32_t webConnects()      { return g_ws_connects; }
uint32_t webDisconnects()   { return g_ws_disconnects; }

// Purga clientes WebSocket muertos; llamar ~1 vez por segundo desde loop().
void webCleanupTick() {
  g_ws.cleanupClients();

  // Reporta desde loop() los eventos WS acumulados en el task asincrono.
  static uint32_t seen_con = 0, seen_dis = 0, seen_drop = 0;
  uint32_t con = g_ws_connects, dis = g_ws_disconnects, drop = g_ws_dropped_frames;
  if (con != seen_con || dis != seen_dis || drop != seen_drop) {
    logLine("WS: conexiones=%lu desconexiones=%lu tramas_descartadas=%lu vivos=%u",
            (unsigned long)con, (unsigned long)dis, (unsigned long)drop,
            (unsigned)g_ws.count());
    seen_con = con; seen_dis = dis; seen_drop = drop;
  }
}

#endif // WEB_SERVER_H
