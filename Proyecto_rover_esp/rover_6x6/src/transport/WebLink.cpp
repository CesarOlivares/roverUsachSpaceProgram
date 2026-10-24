#include "WebLink.h"

#include <WiFi.h>
#include <string.h>
#include "Dispatch.h"
#include "WebAssets.h"
#include "../view/TelemetryView.h"
#include "../util/Log.h"

#if !CONFIG_HTTPD_WS_SUPPORT
#error "El core de Arduino instalado no trae WebSocket en esp_http_server. Usa arduino-esp32 3.x."
#endif

#if ROVER_EXAMPLE_SECRETS
#warning "Falta src/config/Secrets.h: el SoftAP usa la clave publica de ejemplo. Copia Secrets.example.h a Secrets.h."
#endif

/* ------------------------------------------------------------------------ */
/* SoftAP                                                                    */
/* ------------------------------------------------------------------------ */

bool WebLink::startSoftAp()
{
    IPAddress ip(cfg::kApIp[0], cfg::kApIp[1], cfg::kApIp[2], cfg::kApIp[3]);
    IPAddress mask(cfg::kApMask[0], cfg::kApMask[1], cfg::kApMask[2], cfg::kApMask[3]);

    WiFi.mode(WIFI_AP);
    if (!WiFi.softAPConfig(ip, ip, mask)) return false;
    if (!WiFi.softAP(ROVER_AP_SSID, ROVER_AP_PASS, cfg::kApChannel, 0, cfg::kApMaxConn)) return false;

    // El rover manda a 20 Hz: el ahorro de energia de Wi-Fi solo anadiria jitter.
    WiFi.setSleep(false);

    Log::info("LINK", "SoftAP levantado. SSID: %s | Panel en http://%s/",
              ROVER_AP_SSID, ip.toString().c_str());
    if (ROVER_EXAMPLE_SECRETS) {
        Log::warn("LINK", "El SoftAP usa la clave de ejemplo del repositorio. Crea src/config/Secrets.h.");
    }
    return true;
}

/* ------------------------------------------------------------------------ */
/* HTTP: panel embebido                                                      */
/* ------------------------------------------------------------------------ */

esp_err_t WebLink::staticHandler(httpd_req_t *req)
{
    // Sin la query: "/app.js?v=2" -> "/app.js".
    char path[96];
    size_t n = strcspn(req->uri, "?#");
    if (n >= sizeof(path)) n = sizeof(path) - 1;
    memcpy(path, req->uri, n);
    path[n] = '\0';

    const char *wanted = (strcmp(path, "/") == 0) ? "/index.html" : path;

    for (size_t i = 0; i < kWebAssetCount; i++) {
        const WebAsset &a = kWebAssets[i];
        if (strcmp(a.uri, wanted) != 0) continue;

        httpd_resp_set_type(req, a.mime);
        // Sin cache: si se reflashea con una UI nueva, el navegador no debe servir
        // la anterior desde su cache.
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_send(req, (const char *)a.data, (ssize_t)a.len);
    }

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No existe");
    return ESP_OK;
}

/* ------------------------------------------------------------------------ */
/* WebSocket                                                                 */
/* ------------------------------------------------------------------------ */

esp_err_t WebLink::wsHandler(httpd_req_t *req)
{
    WebLink *self = static_cast<WebLink *>(req->user_ctx);

    if (req->method == HTTP_GET) {
        Log::info("LINK", "Cliente WebSocket conectado (fd %d).", httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) return ret;
    if (frame.len == 0 || frame.len > cfg::kMaxLineLen) return ESP_OK;

    uint8_t buf[cfg::kMaxLineLen + 1];
    frame.payload = buf;
    ret = httpd_ws_recv_frame(req, &frame, frame.len);
    if (ret != ESP_OK) return ESP_OK;
    buf[frame.len] = '\0';

    // Un cliente puede agrupar varias tramas JSON separadas por salto de linea.
    char *save = nullptr;
    for (char *line = strtok_r((char *)buf, "\n", &save);
         line != nullptr;
         line = strtok_r(nullptr, "\n", &save)) {
        if (*line) dispatchLine(self->ctrl_, line);
    }
    return ESP_OK;
}

/*
 * Difunde a todos los clientes WebSocket. httpd_get_client_list devuelve solo
 * los sockets vivos, asi que las desconexiones se limpian solas: no hace falta
 * cachear un fd que se quede colgado apuntando a un cliente muerto.
 */
void WebLink::broadcast(const char *payload, size_t len)
{
    if (!server_) return;

    int    fds[cfg::kWsMaxClients + 2];
    size_t count = sizeof(fds) / sizeof(fds[0]);
    if (httpd_get_client_list(server_, &count, fds) != ESP_OK) return;

    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.payload = (uint8_t *)payload;
    frame.len     = len;
    frame.type    = HTTPD_WS_TYPE_TEXT;

    for (size_t i = 0; i < count; i++) {
        if (httpd_ws_get_fd_info(server_, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(server_, fds[i], &frame);
        }
    }
}

int WebLink::clientCount()
{
    if (!server_) return 0;

    int    fds[cfg::kWsMaxClients + 2];
    size_t count = sizeof(fds) / sizeof(fds[0]);
    if (httpd_get_client_list(server_, &count, fds) != ESP_OK) return 0;

    int ws = 0;
    for (size_t i = 0; i < count; i++) {
        if (httpd_ws_get_fd_info(server_, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) ws++;
    }
    return ws;
}

void WebLink::telemetryTask(void *arg) { static_cast<WebLink *>(arg)->telemetryLoop(); }

void WebLink::telemetryLoop()
{
    static char buf[cfg::kTelemetryBufLen];
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        size_t len = TelemetryView::render(ctrl_.snapshot(), clientCount(), buf, sizeof(buf));
        if (len > 0) broadcast(buf, len);
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(cfg::kTelemetryPeriodMs));
    }
}

/* ------------------------------------------------------------------------ */
/* Arranque                                                                  */
/* ------------------------------------------------------------------------ */

bool WebLink::begin()
{
    if (!startSoftAp()) {
        Log::warn("LINK", "No se pudo levantar el SoftAP.");
        return false;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = cfg::kHttpPort;
    config.ctrl_port        = 32768;
    config.max_uri_handlers = 4;
    // Handlers + WebSockets abiertos a la vez.
    config.max_open_sockets = cfg::kWsMaxClients + 2;
    config.lru_purge_enable = true;
    // Un solo handler "/*" sirve todos los archivos del panel.
    config.uri_match_fn     = httpd_uri_match_wildcard;

    esp_err_t ret = httpd_start(&server_, &config);
    if (ret != ESP_OK) {
        Log::warn("LINK", "httpd_start fallo: %s", esp_err_to_name(ret));
        server_ = nullptr;
        return false;
    }

    // /ws va primero: los handlers se prueban en orden de registro.
    httpd_uri_t ws_uri = {};
    ws_uri.uri          = "/ws";
    ws_uri.method       = HTTP_GET;
    ws_uri.handler      = wsHandler;
    ws_uri.user_ctx     = this;
    ws_uri.is_websocket = true;
    httpd_register_uri_handler(server_, &ws_uri);

    httpd_uri_t files_uri = {};
    files_uri.uri      = "/*";
    files_uri.method   = HTTP_GET;
    files_uri.handler  = staticHandler;
    files_uri.user_ctx = this;
    httpd_register_uri_handler(server_, &files_uri);

    if (xTaskCreatePinnedToCore(telemetryTask, "link_ws_tlm", 4096, this, 4, nullptr, 0) != pdPASS) {
        return false;
    }

    Log::info("LINK", "Servidor web listo en el puerto %u (UI + /ws, %u archivos).",
              (unsigned)cfg::kHttpPort, (unsigned)kWebAssetCount);
    return true;
}
