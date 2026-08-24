#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "pinout.h"
#include "link.h"

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

// Un frame puede traer varias ordenes separadas por '\n'.
#define WS_FRAME_MAX 512

/*
 * Contadores de eventos WS. Se incrementan en el task asincrono y los reporta
 * webLogTick() desde loop(): asi el log nunca se entrelaza con la telemetria.
 */
static volatile uint32_t g_ws_connects = 0;
static volatile uint32_t g_ws_disconnects = 0;
static volatile uint32_t g_ws_dropped_frames = 0;   // tramas no final/fragmentadas o desbordadas

static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  (void)server; (void)client;
  if (type == WS_EVT_CONNECT)    { g_ws_connects++;    return; }
  if (type == WS_EVT_DISCONNECT) { g_ws_disconnects++; return; }
  if (type != WS_EVT_DATA) return;

  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  // Solo tramas de texto completas en un unico frame; el panel nunca fragmenta.
  if (!info->final || info->index != 0 || info->len != len) {
    g_ws_dropped_frames++;
    return;
  }
  if (info->opcode != WS_TEXT) return;
  if (len == 0 || len >= WS_FRAME_MAX) {
    g_ws_dropped_frames++;
    g_state.rxErr++;
    return;
  }

  char buf[WS_FRAME_MAX];
  memcpy(buf, data, len);
  buf[len] = '\0';

  char *save = nullptr;
  for (char *tok = strtok_r(buf, "\n", &save); tok; tok = strtok_r(nullptr, "\n", &save)) {
    linkDispatchLine(tok);
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
