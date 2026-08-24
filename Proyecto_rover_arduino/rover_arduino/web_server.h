#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "pinout.h"
#include "link.h"
#include "web_pages.h"

/*
 * SoftAP + panel embebido + WebSocket de comandos (ESPAsyncWebServer).
 *
 * El AP conserva las credenciales y la IP del firmware ESP-IDF (192.168.4.1,
 * la de fabrica del SoftAP), asi el panel se conecta igual que siempre.
 *
 * Los callbacks del servidor son asincronos: dentro solo se parsea JSON y se
 * escriben PWMs (microsegundos). Nada de delay() ni de esperas; la telemetria
 * sale unicamente desde loop().
 */

static AsyncWebServer g_http(80);
static AsyncWebSocket g_ws("/ws");

// Un frame puede traer varias ordenes separadas por '\n'.
#define WS_FRAME_MAX 512

static void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client,
                      AwsEventType type, void *arg, uint8_t *data, size_t len) {
  (void)server; (void)client;
  if (type != WS_EVT_DATA) return;

  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  // Solo tramas de texto completas en un unico frame; el panel nunca fragmenta.
  if (!info->final || info->index != 0 || info->len != len) return;
  if (info->opcode != WS_TEXT) return;
  if (len == 0 || len >= WS_FRAME_MAX) {
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

static void sendEmbedded(AsyncWebServerRequest *req, const char *ctype,
                         const uint8_t *data, size_t len) {
  AsyncWebServerResponse *res = req->beginResponse(200, ctype, data, len);
  req->send(res);
}

void webInit() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS, 1 /*canal*/, 0 /*visible*/, 4 /*clientes max*/);

  g_ws.onEvent(onWsEvent);
  g_http.addHandler(&g_ws);

  g_http.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    sendEmbedded(req, "text/html; charset=utf-8", INDEX_HTML, INDEX_HTML_LEN);
  });
  g_http.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *req) {
    sendEmbedded(req, "text/javascript; charset=utf-8", APP_JS, APP_JS_LEN);
  });
  g_http.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *req) {
    sendEmbedded(req, "text/css; charset=utf-8", STYLE_CSS, STYLE_CSS_LEN);
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
}

#endif // WEB_SERVER_H
