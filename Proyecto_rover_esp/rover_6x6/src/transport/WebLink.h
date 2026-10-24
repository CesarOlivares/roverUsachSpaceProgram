#pragma once

#include "esp_http_server.h"
#include "../controller/CommandController.h"

/*
 * Addon web: SoftAP + servidor HTTP que sirve el panel embebido y expone /ws.
 *
 * El panel va dentro del binario (WebAssets.h, generado con
 * tools/embed_web.ps1), asi que un solo "Subir" desde el IDE deja la interfaz
 * en http://192.168.4.1/ sin sistema de archivos ni paso de flasheo extra.
 */
class WebLink {
public:
    explicit WebLink(CommandController &ctrl) : ctrl_(ctrl) {}

    bool begin();

    /** Clientes WebSocket vivos. */
    int clientCount();

private:
    bool startSoftAp();
    void broadcast(const char *payload, size_t len);

    static esp_err_t staticHandler(httpd_req_t *req);
    static esp_err_t wsHandler(httpd_req_t *req);
    static void      telemetryTask(void *arg);
    void             telemetryLoop();

    CommandController &ctrl_;
    httpd_handle_t     server_ = nullptr;
};
