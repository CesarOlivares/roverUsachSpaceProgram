#include "WifiLink.h"

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <stdarg.h>
#include <string.h>
#include "Dispatch.h"
#include "WebAssets.h"
#include "../view/TelemetryView.h"
#include "../util/Log.h"

#if __has_include("../config/Secrets.h")
#include "../config/Secrets.h"
#define ROVER_EXAMPLE_SECRETS 0
#else
#include "../config/Secrets.example.h"
#define ROVER_EXAMPLE_SECRETS 1
#warning "Falta src/config/Secrets.h: el punto de acceso usa la clave publica de ejemplo."
#endif

#if !CONFIG_HTTPD_WS_SUPPORT
#error "El core de Arduino instalado no trae WebSocket en esp_http_server. Usa arduino-esp32 3.x."
#endif

namespace {

WifiLink *s_self = nullptr;

// Motivo de la ultima desconexion del modo estacion (lo escribe el evento de Wi-Fi).
volatile uint8_t s_lastReason = 0;

constexpr size_t kWsBufLen  = 1024;   // un panel puede agrupar varias lineas en una trama
constexpr size_t kOutBufLen = 1024;
constexpr int    kScanMax   = 12;

/* --- JSON minimo ---------------------------------------------------------- */

// Lee el valor de cadena de "key". Devuelve false si no esta o no cabe.
bool jsonString(const char *line, const char *key, char *out, size_t outLen)
{
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(line, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    if (*p++ != ':') return false;
    while (*p == ' ') p++;
    if (*p++ != '"') return false;

    size_t n = 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            c = *p++;
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
            else if (c == 'u') { for (int i = 0; i < 4 && *p; i++) p++; c = '?'; }
            else if (c == '\0') return false;
        }
        if (n + 1 >= outLen) return false;
        out[n++] = c;
    }
    if (*p != '"') return false;
    out[n] = '\0';
    return true;
}

// Copia src escapado para meterlo entre comillas en un JSON.
void jsonEscape(const char *src, char *out, size_t outLen)
{
    size_t n = 0;
    for (const char *c = src; *c && n + 7 < outLen; c++) {
        unsigned char u = (unsigned char)*c;
        if (u == '"' || u == '\\') { out[n++] = '\\'; out[n++] = (char)u; }
        else if (u < 0x20)         { n += snprintf(out + n, outLen - n, "\\u%04x", u); }
        else                       { out[n++] = (char)u; }
    }
    out[n] = '\0';
}

const char *reasonText(uint8_t r)
{
    switch (r) {
    case WIFI_REASON_NO_AP_FOUND:          return "no se encuentra la red";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:    return "clave incorrecta";
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_ASSOC_TOOMANY:        return "la red rechazo la conexion";
    case WIFI_REASON_BEACON_TIMEOUT:       return "senal perdida";
    case 0:                                return "sin respuesta";
    default:                               return "fallo de conexion";
    }
}

const char *staText(int st)
{
    switch (st) {
    case 0:  return "off";
    case 1:  return "connecting";
    case 2:  return "connected";
    default: return "waiting";
    }
}

}  // namespace

/* ------------------------------------------------------------------------ */
/* Ordenes WIFI_* (llegan desde la tarea del cable o del servidor web)       */
/* ------------------------------------------------------------------------ */

bool WifiLink::handleLine(const char *line)
{
    WifiLink *self = s_self;
    if (!self) return false;

    char action[16];
    if (!jsonString(line, "action", action, sizeof(action))) return false;

    if (strcmp(action, "WIFI_SET") == 0) {
        char ssid[33] = {0}, pass[65] = {0};
        if (!jsonString(line, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
            self->queueRequest(Req::Status, nullptr, nullptr);
            self->setMessage("Falta el nombre de la red (maximo 32 caracteres).");
            return true;
        }
        jsonString(line, "pass", pass, sizeof(pass));
        if (pass[0] != '\0' && strlen(pass) < 8) {
            self->setMessage("La clave WPA tiene al menos 8 caracteres.");
            self->queueRequest(Req::Status, nullptr, nullptr);
            return true;
        }
        self->queueRequest(Req::Set, ssid, pass);
        return true;
    }
    if (strcmp(action, "WIFI_FORGET") == 0) { self->queueRequest(Req::Forget, nullptr, nullptr); return true; }
    if (strcmp(action, "WIFI_SCAN") == 0)   { self->queueRequest(Req::Scan, nullptr, nullptr);   return true; }
    if (strcmp(action, "WIFI_STATUS") == 0) { self->queueRequest(Req::Status, nullptr, nullptr); return true; }
    return false;
}

void WifiLink::queueRequest(Req req, const char *ssid, const char *pass)
{
    portENTER_CRITICAL(&reqLock_);
    // Una peticion de estado no pisa a una de verdad que aun no se atendio.
    if (!(req == Req::Status && req_ != Req::None)) {
        req_ = req;
        strlcpy(reqSsid_, ssid ? ssid : "", sizeof(reqSsid_));
        strlcpy(reqPass_, pass ? pass : "", sizeof(reqPass_));
    }
    portEXIT_CRITICAL(&reqLock_);
}

/* ------------------------------------------------------------------------ */
/* Credenciales (NVS)                                                        */
/* ------------------------------------------------------------------------ */

void WifiLink::loadCredentials()
{
    Preferences prefs;
    // Si la red se configuro desde el panel (aunque sea para olvidarla), manda
    // sobre la de Secrets.h.
    if (prefs.begin("wifi", true) && prefs.isKey("ssid")) {
        prefs.getString("ssid", staSsid_, sizeof(staSsid_));
        prefs.getString("pass", staPass_, sizeof(staPass_));
    } else {
        strlcpy(staSsid_, ROVER_STA_SSID, sizeof(staSsid_));
        strlcpy(staPass_, ROVER_STA_PASS, sizeof(staPass_));
    }
    prefs.end();
}

void WifiLink::saveCredentials(const char *ssid, const char *pass)
{
    Preferences prefs;
    if (prefs.begin("wifi", false)) {
        prefs.putString("ssid", ssid);
        prefs.putString("pass", pass);
        prefs.end();
    }
    strlcpy(staSsid_, ssid, sizeof(staSsid_));
    strlcpy(staPass_, pass, sizeof(staPass_));
}

/* ------------------------------------------------------------------------ */
/* Punto de acceso y modo estacion                                           */
/* ------------------------------------------------------------------------ */

bool WifiLink::startAp()
{
    IPAddress ip(cfg::kApIp[0], cfg::kApIp[1], cfg::kApIp[2], cfg::kApIp[3]);
    IPAddress mask(cfg::kApMask[0], cfg::kApMask[1], cfg::kApMask[2], cfg::kApMask[3]);

    WiFi.persistent(false);              // las credenciales las guarda este modulo, no el driver
    WiFi.setHostname(cfg::kHostname);
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(false);        // los reintentos los lleva stepSta(), espaciados
    if (!WiFi.softAPConfig(ip, ip, mask)) return false;
    if (!WiFi.softAP(ROVER_AP_SSID, ROVER_AP_PASS, cfg::kApChannel, 0, cfg::kApMaxConn)) return false;

    // El mando va a 20 Hz: el ahorro de energia solo anadiria latencia.
    WiFi.setSleep(false);
    WiFi.setTxPower((wifi_power_t)cfg::kWifiTxPowerQdbm);

    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
        s_lastReason = info.wifi_sta_disconnected.reason;
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    Log::info("WIFI", "Punto de acceso '%s' listo. Panel en http://%s/",
              ROVER_AP_SSID, ip.toString().c_str());
    if (ROVER_EXAMPLE_SECRETS) {
        Log::warn("WIFI", "El punto de acceso usa la clave de ejemplo del repositorio. Crea src/config/Secrets.h.");
    }
    return true;
}

void WifiLink::startSta()
{
    if (staSsid_[0] == '\0') {
        sta_ = Sta::Off;
        return;
    }
    s_lastReason = 0;
    lastReason_  = 0;
    WiFi.disconnect(false, false);       // false: el punto de acceso sigue arriba
    WiFi.begin(staSsid_, staPass_[0] ? staPass_ : nullptr);
    sta_       = Sta::Connecting;
    staSince_  = millis();
    attemptAt_ = staSince_;
    Log::info("WIFI", "Uniendose a la red '%s'...", staSsid_);
}

void WifiLink::stepSta(uint32_t now)
{
    wl_status_t st = WiFi.status();

    switch (sta_) {
    case Sta::Off:
        break;

    case Sta::Connecting:
        if (s_lastReason != 0) lastReason_ = s_lastReason;
        // Sin reconexion automatica, un primer rechazo (tipico en redes
        // concurridas) dejaba sin intentar el resto de la ventana: se reintenta.
        if (st != WL_CONNECTED && s_lastReason != 0 && now - attemptAt_ > 4000) {
            Log::warn("WIFI", "'%s' rechazo el intento: %s (motivo %u). Reintentando...",
                      staSsid_, reasonText(s_lastReason), (unsigned)s_lastReason);
            s_lastReason = 0;
            attemptAt_   = now;
            WiFi.begin(staSsid_, staPass_[0] ? staPass_ : nullptr);
        }
        if (st == WL_CONNECTED) {
            sta_ = Sta::Connected;
            setMessage("Conectado a '%s': panel en http://%s/ o http://%s.local/",
                       staSsid_, WiFi.localIP().toString().c_str(), cfg::kHostname);
            Log::info("WIFI", "%s", msg_);
        } else if (now - staSince_ > cfg::kStaConnectMs) {
            uint8_t r = lastReason_;
            WiFi.disconnect(false, false);
            sta_      = Sta::Waiting;
            staSince_ = now;
            setMessage("No se pudo unir a '%s': %s (motivo %u). Reintento en %u s.",
                       staSsid_, reasonText(r), (unsigned)r, (unsigned)(cfg::kStaRetryMs / 1000));
            Log::warn("WIFI", "%s", msg_);
        }
        break;

    case Sta::Connected:
        if (st != WL_CONNECTED) {
            setMessage("Se perdio la red '%s' (%s). Reconectando...", staSsid_, reasonText(s_lastReason));
            Log::warn("WIFI", "%s", msg_);
            startSta();
        }
        break;

    case Sta::Waiting:
        if (now - staSince_ >= cfg::kStaRetryMs && !scanning_) startSta();
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* Tarea del Wi-Fi                                                           */
/* ------------------------------------------------------------------------ */

void WifiLink::serveRequest(uint32_t now)
{
    (void)now;
    Req  req;
    char ssid[33], pass[65];
    portENTER_CRITICAL(&reqLock_);
    req  = req_;
    req_ = Req::None;
    memcpy(ssid, reqSsid_, sizeof(ssid));
    memcpy(pass, reqPass_, sizeof(pass));
    portEXIT_CRITICAL(&reqLock_);

    switch (req) {
    case Req::None:
        return;

    case Req::Set:
    case Req::Forget:
        // Cambiar de red con el rover armado podria cortar el enlace del que
        // conduce: hay que desarmar primero.
        if (ctrl_.snapshot().armed) {
            setMessage("Desarma el rover antes de cambiar la red Wi-Fi.");
            break;
        }
        if (req == Req::Set) {
            saveCredentials(ssid, pass);
            setMessage("Red '%s' guardada. Conectando...", ssid);
            startSta();
        } else {
            saveCredentials("", "");
            WiFi.disconnect(false, false);
            sta_ = Sta::Off;
            setMessage("Red olvidada. El rover queda solo con su punto de acceso '%s'.", ROVER_AP_SSID);
        }
        Log::info("WIFI", "%s", msg_);
        break;

    case Req::Scan:
        if (scanning_) break;
        // Un intento de conexion en curso hace fallar el escaneo: se aparca.
        if (sta_ == Sta::Connecting) {
            WiFi.disconnect(false, false);
            sta_      = Sta::Waiting;
            staSince_ = millis();
        }
        if (WiFi.scanNetworks(true, false) == WIFI_SCAN_FAILED) {
            setMessage("No se pudo iniciar la busqueda de redes.");
        } else {
            scanning_ = true;
            setMessage("Buscando redes...");
        }
        break;

    case Req::Status:
        break;
    }
    publishStatus();
}

void WifiLink::finishScan()
{
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    scanning_ = false;

    static char out[kOutBufLen];
    size_t len = (size_t)snprintf(out, sizeof(out), "{\"type\":\"wifi_scan\",\"nets\":[");
    int    shown = 0;

    if (n > 0) {
        // Las mas fuertes primero, sin repetir nombre (varias antenas de la misma red).
        bool used[64] = {false};
        int  picked[kScanMax];
        int  total = n < 64 ? n : 64;
        for (int k = 0; k < total && shown < kScanMax; k++) {
            int best = -1;
            for (int i = 0; i < total; i++) {
                if (!used[i] && (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best))) best = i;
            }
            used[best] = true;
            String ssid = WiFi.SSID(best);
            if (ssid.length() == 0) continue;           // red oculta
            bool dup = false;
            for (int j = 0; j < shown && !dup; j++) dup = WiFi.SSID(picked[j]) == ssid;
            if (dup) continue;
            picked[shown] = best;

            char esc[80];
            jsonEscape(ssid.c_str(), esc, sizeof(esc));
            int m = snprintf(out + len, sizeof(out) - len, "%s{\"ssid\":\"%s\",\"rssi\":%d,\"ch\":%d,\"open\":%s}",
                             shown ? "," : "", esc, (int)WiFi.RSSI(best), (int)WiFi.channel(best),
                             WiFi.encryptionType(best) == WIFI_AUTH_OPEN ? "true" : "false");
            if (m <= 0 || len + m >= sizeof(out) - 4) break;
            len += m;
            shown++;
        }
    }
    WiFi.scanDelete();
    len += snprintf(out + len, sizeof(out) - len, "]}\n");
    emitLine(out, len);
    setMessage(n < 0 ? "La busqueda de redes fallo." : "Busqueda terminada: %d redes.", shown);
    publishStatus();
}

void WifiLink::setMessage(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg_, sizeof(msg_), fmt, ap);
    va_end(ap);
    msgSeq_++;
}

void WifiLink::publishStatus()
{
    static char out[512];
    char ap[80], sta[80], msg[200];
    jsonEscape(ROVER_AP_SSID, ap, sizeof(ap));
    jsonEscape(staSsid_, sta, sizeof(sta));
    jsonEscape(msg_, msg, sizeof(msg));

    bool connected = sta_ == Sta::Connected;
    int n = snprintf(out, sizeof(out),
        "{\"type\":\"wifi\",\"ap\":\"%s\",\"ap_ip\":\"%s\",\"ap_clients\":%d"
        ",\"sta\":\"%s\",\"sta_state\":\"%s\",\"sta_ip\":\"%s\",\"rssi\":%d,\"ch\":%d"
        ",\"host\":\"%s.local\",\"ws\":%d,\"scanning\":%s,\"msg\":\"%s\",\"msg_seq\":%u}\n",
        ap, WiFi.softAPIP().toString().c_str(), (int)WiFi.softAPgetStationNum(),
        sta, staText((int)sta_), connected ? WiFi.localIP().toString().c_str() : "",
        connected ? (int)WiFi.RSSI() : 0, (int)WiFi.channel(),
        cfg::kHostname, clientCount(), scanning_ ? "true" : "false", msg, (unsigned)msgSeq_);
    if (n > 0 && (size_t)n < sizeof(out)) emitLine(out, (size_t)n);
}

// Lineas propias del Wi-Fi: a los paneles por WebSocket y al cable.
void WifiLink::emitLine(const char *line, size_t len)
{
    broadcast(line, len);
    if (cfg::kLinkSerial) Serial.write((const uint8_t *)line, len);
}

void WifiLink::managerTask(void *arg) { static_cast<WifiLink *>(arg)->managerLoop(); }

void WifiLink::managerLoop()
{
    uint32_t lastStatus = 0;
    startSta();

    while (true) {
        uint32_t now = millis();
        serveRequest(now);
        if (scanning_) finishScan();
        stepSta(now);

        if (now - lastStatus >= cfg::kWifiStatusMs) {
            lastStatus = now;
            publishStatus();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/* ------------------------------------------------------------------------ */
/* HTTP: panel embebido                                                      */
/* ------------------------------------------------------------------------ */

esp_err_t WifiLink::staticHandler(httpd_req_t *req)
{
    char   path[96];
    size_t n = strcspn(req->uri, "?#");
    if (n >= sizeof(path)) n = sizeof(path) - 1;
    memcpy(path, req->uri, n);
    path[n] = '\0';

    const char *wanted = (strcmp(path, "/") == 0) ? "/index.html" : path;

    for (size_t i = 0; i < kWebAssetCount; i++) {
        const WebAsset &a = kWebAssets[i];
        if (strcmp(a.uri, wanted) != 0) continue;

        httpd_resp_set_type(req, a.mime);
        if (a.gzip) httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        // Sin cache: tras reflashear con otra version del panel, que no se sirva la vieja.
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        return httpd_resp_send(req, (const char *)a.data, (ssize_t)a.len);
    }

    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No existe");
    return ESP_OK;
}

/* ------------------------------------------------------------------------ */
/* WebSocket                                                                 */
/* ------------------------------------------------------------------------ */

esp_err_t WifiLink::wsHandler(httpd_req_t *req)
{
    WifiLink *self = static_cast<WifiLink *>(req->user_ctx);

    if (req->method == HTTP_GET) {
        Log::info("WIFI", "Panel conectado por WebSocket (fd %d).", httpd_req_to_sockfd(req));
        self->queueRequest(Req::Status, nullptr, nullptr);   // que vea el estado sin esperar
        return ESP_OK;
    }

    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) return ret;
    if (frame.type != HTTPD_WS_TYPE_TEXT || frame.len == 0) return ESP_OK;

    // El servidor atiende de una peticion en una: el buffer estatico es seguro.
    static uint8_t buf[kWsBufLen + 1];
    if (frame.len > kWsBufLen) {
        self->ctrl_.reportInvalidFrame();
        return ESP_OK;
    }
    frame.payload = buf;
    ret = httpd_ws_recv_frame(req, &frame, frame.len);
    if (ret != ESP_OK) return ESP_OK;
    buf[frame.len] = '\0';

    // Un panel puede agrupar varias tramas JSON separadas por salto de linea.
    char *save = nullptr;
    for (char *line = strtok_r((char *)buf, "\r\n", &save); line; line = strtok_r(nullptr, "\r\n", &save)) {
        if (strlen(line) >= cfg::kMaxLineLen) { self->ctrl_.reportInvalidFrame(); continue; }
        if (*line) dispatchLine(self->ctrl_, line);
    }
    return ESP_OK;
}

void WifiLink::broadcast(const char *payload, size_t len)
{
    if (!server_) return;

    int    fds[cfg::kWsMaxClients + 3];
    size_t count = sizeof(fds) / sizeof(fds[0]);
    if (httpd_get_client_list(server_, &count, fds) != ESP_OK) return;

    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.payload = (uint8_t *)payload;
    frame.len     = len;
    frame.type    = HTTPD_WS_TYPE_TEXT;

    // Telemetria y estado salen de tareas distintas: sin el mutex, dos tramas
    // podrian mezclarse en el mismo socket.
    xSemaphoreTake(sendMtx_, portMAX_DELAY);
    for (size_t i = 0; i < count; i++) {
        if (httpd_ws_get_fd_info(server_, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(server_, fds[i], &frame);
        }
    }
    xSemaphoreGive(sendMtx_);
}

int WifiLink::clientCount()
{
    if (!server_) return 0;

    int    fds[cfg::kWsMaxClients + 3];
    size_t count = sizeof(fds) / sizeof(fds[0]);
    if (httpd_get_client_list(server_, &count, fds) != ESP_OK) return 0;

    int ws = 0;
    for (size_t i = 0; i < count; i++) {
        if (httpd_ws_get_fd_info(server_, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) ws++;
    }
    return ws;
}

void WifiLink::telemetryTask(void *arg) { static_cast<WifiLink *>(arg)->telemetryLoop(); }

void WifiLink::telemetryLoop()
{
    static char buf[cfg::kTelemetryBufLen];
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        int clients = clientCount();
        if (clients > 0) {
            RoverSnapshot s = ctrl_.snapshot();
            s.clients = clients;
            size_t len = TelemetryView::renderJson(s, buf, sizeof(buf));
            if (len > 0) broadcast(buf, len);
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(cfg::kTelemetryPeriodMs));
    }
}

/* ------------------------------------------------------------------------ */
/* Arranque                                                                  */
/* ------------------------------------------------------------------------ */

bool WifiLink::startServer()
{
    httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
    config.server_port      = cfg::kHttpPort;
    config.ctrl_port        = 32768;
    config.max_uri_handlers = 4;
    config.max_open_sockets = cfg::kWsMaxClients + 3;
    config.lru_purge_enable = true;
    config.uri_match_fn     = httpd_uri_match_wildcard;
    config.stack_size       = 6144;
    config.core_id          = 0;

    esp_err_t ret = httpd_start(&server_, &config);
    if (ret != ESP_OK) {
        Log::warn("WIFI", "httpd_start fallo: %s", esp_err_to_name(ret));
        server_ = nullptr;
        return false;
    }

    // /ws va primero: los handlers se prueban en orden de registro.
    httpd_uri_t ws_uri  = {};
    ws_uri.uri          = "/ws";
    ws_uri.method       = HTTP_GET;
    ws_uri.handler      = wsHandler;
    ws_uri.user_ctx     = this;
    ws_uri.is_websocket = true;
    httpd_register_uri_handler(server_, &ws_uri);

    httpd_uri_t files_uri = {};
    files_uri.uri         = "/*";
    files_uri.method      = HTTP_GET;
    files_uri.handler     = staticHandler;
    files_uri.user_ctx    = this;
    httpd_register_uri_handler(server_, &files_uri);
    return true;
}

bool WifiLink::begin()
{
    s_self   = this;
    sendMtx_ = xSemaphoreCreateMutex();
    if (!sendMtx_) return false;

    loadCredentials();
    if (!startAp()) {
        Log::warn("WIFI", "No se pudo levantar el punto de acceso.");
        return false;
    }
    if (MDNS.begin(cfg::kHostname)) MDNS.addService("http", "tcp", cfg::kHttpPort);

    if (!startServer()) return false;
    wifiLineHook() = &WifiLink::handleLine;

    if (staSsid_[0]) setMessage("Red guardada: '%s'.", staSsid_);
    else             setMessage("Sin red guardada: solo punto de acceso '%s'.", ROVER_AP_SSID);

    bool ok = xTaskCreatePinnedToCore(managerTask, "wifi_mgr", 4096, this, 2, nullptr, 0) == pdPASS;
    ok = ok && xTaskCreatePinnedToCore(telemetryTask, "wifi_tlm", 4096, this, 4, nullptr, 0) == pdPASS;

    Log::info("WIFI", "Servidor web listo (panel + /ws, %u archivos).", (unsigned)kWebAssetCount);
    return ok;
}
