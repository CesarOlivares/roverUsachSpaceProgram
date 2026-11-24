#pragma once

#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "../controller/CommandController.h"

/*
 * Enlace de emergencia por Wi-Fi.
 *
 *  - Punto de acceso propio, siempre encendido (ROVER_AP_SSID en Secrets.h):
 *    el panel entra en http://192.168.4.1/.
 *  - Ademas se une a una red existente (modo estacion). La red se elige desde
 *    el panel y queda guardada en la NVS del rover; la primera vez se usa la
 *    de Secrets.h. En esa red el panel esta en http://rover6x6.local/ o en la
 *    IP que el rover publica en su estado.
 *  - Sirve el panel embebido (WebAssets.h, generado con tools/embed_web.ps1) y
 *    el mando por WebSocket en /ws: las mismas lineas JSON que por cable.
 *
 * Ordenes propias (por cable o WebSocket; no por radio):
 *   {"action":"WIFI_SET","ssid":"...","pass":"..."}  guardar red y unirse
 *   {"action":"WIFI_FORGET"}                          olvidar red (solo punto de acceso)
 *   {"action":"WIFI_SCAN"}                            buscar redes -> {"type":"wifi_scan",...}
 *   {"action":"WIFI_STATUS"}                          estado inmediato
 * Cada segundo se publica {"type":"wifi",...} por WebSocket y por cable.
 */
class WifiLink {
public:
    explicit WifiLink(CommandController &ctrl) : ctrl_(ctrl) {}

    bool begin();

private:
    enum class Sta : uint8_t { Off, Connecting, Connected, Waiting };
    enum class Req : uint8_t { None, Set, Forget, Scan, Status };

    static bool handleLine(const char *line);
    void        queueRequest(Req req, const char *ssid, const char *pass);

    bool startAp();
    bool startServer();
    void loadCredentials();
    void saveCredentials(const char *ssid, const char *pass);
    void startSta();
    void stepSta(uint32_t now);
    void serveRequest(uint32_t now);
    void finishScan();
    void publishStatus();
    void setMessage(const char *fmt, ...);
    void emitLine(const char *line, size_t len);

    void broadcast(const char *payload, size_t len);
    int  clientCount();

    static esp_err_t staticHandler(httpd_req_t *req);
    static esp_err_t wsHandler(httpd_req_t *req);
    static void      telemetryTask(void *arg);
    static void      managerTask(void *arg);
    void             telemetryLoop();
    void             managerLoop();

    CommandController &ctrl_;
    httpd_handle_t     server_  = nullptr;
    SemaphoreHandle_t  sendMtx_ = nullptr;   // una trama WebSocket a la vez por socket

    // Peticiones desde otras tareas (cable, WebSocket) hacia la tarea del Wi-Fi.
    portMUX_TYPE reqLock_    = portMUX_INITIALIZER_UNLOCKED;
    Req          req_        = Req::None;
    char         reqSsid_[33] = {0};
    char         reqPass_[65] = {0};

    // Solo los toca la tarea del Wi-Fi.
    char     staSsid_[33] = {0};
    char     staPass_[65] = {0};
    Sta      sta_         = Sta::Off;
    uint32_t staSince_    = 0;
    uint32_t attemptAt_   = 0;   // ultimo WiFi.begin() dentro de la ventana de conexion
    uint8_t  lastReason_  = 0;   // ultimo motivo de rechazo visto en esta ventana
    bool     scanning_    = false;
    char     msg_[96]     = {0};
    uint32_t msgSeq_      = 0;
};
