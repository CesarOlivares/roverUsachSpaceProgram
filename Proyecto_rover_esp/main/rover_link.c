#include "rover_link.h"
#include "mcpwm_motors.h"
#include "stepper_drv.h"
#include "pca9685_servo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

#if ROVER_LINK_UART
#include "driver/uart.h"
#endif

#if ROVER_LINK_HTTP
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "lwip/ip4_addr.h"
#endif

static const char *TAG = "LINK";

#define TELEMETRY_PERIOD_MS   (1000 / ROVER_TELEMETRY_HZ)
#define FAILSAFE_POLL_MS      25
#define MAX_LINE_LEN          256
#define TELEMETRY_BUF_LEN     384

// Geometria de direccion: 45 grados izq <- 90 centro -> 135 der.
#define STEER_CENTER_DEG      90.0f
#define STEER_THROW_DEG       45.0f

/* ------------------------------------------------------------------------ */
/* Estado compartido                                                         */
/* ------------------------------------------------------------------------ */

/*
 * Estos campos los tocan la tarea de RX por UART, el handler HTTP, la tarea de
 * failsafe y las de telemetria, repartidas entre los dos nucleos.
 * g_last_packet_us son 64 bits, asi que ni su lectura es atomica en una MCU de
 * 32 bits: hace falta seccion critica, no solo volatile.
 */
static portMUX_TYPE g_state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool     g_armed          = false;
static int64_t  g_last_packet_us = 0;
static uint32_t g_rx_ok          = 0;   // tramas JSON aceptadas
static uint32_t g_rx_err         = 0;   // lineas descartadas (JSON invalido / desbordadas)
static uint32_t g_last_seq       = 0;   // secuencia del cliente, se devuelve para medir RTT

/*
 * Mando exclusivo.
 *
 * Con el addon web puede haber varios paneles conectados a la vez, y todos
 * emiten a 20 Hz. Sin arbitraje, dos clientes armados mandarian DRIVE
 * contradictorios en el mismo ciclo y los motores oscilarian entre ambas
 * consignas. El primero que actua se queda el mando; los demas siguen viendo
 * la telemetria pero no pueden mover nada.
 *
 * Se libera con DISARM, con STOP o cuando salta el failsafe. Parar siempre lo
 * puede cualquiera: una parada de emergencia no se le niega a nadie.
 */
#define OWNER_ID_LEN 24
static char g_owner[OWNER_ID_LEN] = {0};   // cadena vacia = mando libre

static inline void state_mark_packet(void)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&g_state_lock);
    g_last_packet_us = now;
    portEXIT_CRITICAL(&g_state_lock);
}

static inline bool state_is_armed(void)
{
    portENTER_CRITICAL(&g_state_lock);
    bool armed = g_armed;
    portEXIT_CRITICAL(&g_state_lock);
    return armed;
}

static inline void state_set_armed(bool armed)
{
    portENTER_CRITICAL(&g_state_lock);
    g_armed = armed;
    portEXIT_CRITICAL(&g_state_lock);
}

static inline void state_count_error(void)
{
    portENTER_CRITICAL(&g_state_lock);
    g_rx_err++;
    portEXIT_CRITICAL(&g_state_lock);
}

/*
 * Concede el mando a @p client si esta libre o ya era suyo.
 * Un cliente sin identificar (client == NULL) se trata como "consola anonima":
 * solo puede actuar si nadie tiene el mando, y no lo reclama. Asi el enlace por
 * cable sigue funcionando aunque se hable con el rover a mano por terminal.
 */
static bool state_claim_control(const char *client)
{
    /*
     * El id se copia saneado y acotado FUERA de la seccion critica: viene de la
     * red, se reenvia dentro de la telemetria JSON y una comilla o una barra
     * romperia la trama de todos los clientes. Solo se aceptan alfanumericos.
     */
    char id[OWNER_ID_LEN] = {0};
    if (client) {
        size_t n = 0;
        for (const char *p = client; *p && n < sizeof(id) - 1; p++) {
            if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                (*p >= '0' && *p <= '9')) {
                id[n++] = *p;
            }
        }
        id[n] = '\0';
    }

    bool granted;
    portENTER_CRITICAL(&g_state_lock);
    if (g_owner[0] == '\0') {
        // Mando libre: lo toma quien se identifique. Un cliente anonimo actua
        // pero no lo reclama, para no bloquear al resto para siempre.
        memcpy(g_owner, id, sizeof(g_owner));
        granted = true;
    } else {
        granted = (id[0] != '\0') && (strcmp(g_owner, id) == 0);
    }
    portEXIT_CRITICAL(&g_state_lock);
    return granted;
}

static inline void state_release_control(void)
{
    portENTER_CRITICAL(&g_state_lock);
    g_owner[0] = '\0';
    portEXIT_CRITICAL(&g_state_lock);
}

static inline int64_t state_silence_us(void)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&g_state_lock);
    int64_t last = g_last_packet_us;
    portEXIT_CRITICAL(&g_state_lock);
    return now - last;
}

/* ------------------------------------------------------------------------ */
/* Parada segura                                                             */
/* ------------------------------------------------------------------------ */

// Corta toda la potencia. Segura de llamar desde cualquier tarea.
static void rover_safe_stop(void)
{
    mcpwm_motors_stop_all();
    stepper_drv_stop();
    pca9685_center_all();
}

/* ------------------------------------------------------------------------ */
/* Dispatcher de comandos (comun a UART y WebSocket)                         */
/* ------------------------------------------------------------------------ */

static void handle_command(cJSON *root)
{
    cJSON *action = cJSON_GetObjectItem(root, "action");
    if (!cJSON_IsString(action) || !action->valuestring) {
        state_count_error();
        return;
    }

    // Cualquier trama valida cuenta como senal de vida para el failsafe.
    state_mark_packet();

    cJSON *seq = cJSON_GetObjectItem(root, "seq");
    portENTER_CRITICAL(&g_state_lock);
    g_rx_ok++;
    if (cJSON_IsNumber(seq)) g_last_seq = (uint32_t)seq->valuedouble;
    portEXIT_CRITICAL(&g_state_lock);

    const char *act = action->valuestring;

    if (strcmp(act, "HEARTBEAT") == 0) {
        return;                                  // ya renovo el failsafe
    }

    // Parar es universal: no pasa por el arbitraje del mando y ademas lo libera.
    if (strcmp(act, "DISARM") == 0 || strcmp(act, "STOP") == 0) {
        state_set_armed(false);
        rover_safe_stop();
        state_release_control();
        ESP_LOGI(TAG, "ROVER DESARMADO.");
        return;
    }

    // A partir de aqui todo son ordenes que mueven hardware: exigen el mando.
    cJSON *client_item = cJSON_GetObjectItem(root, "client");
    const char *client = cJSON_IsString(client_item) ? client_item->valuestring : NULL;

    if (!state_claim_control(client)) {
        // Otro operador esta al mando. Se descarta en silencio: a 20 Hz, avisar
        // por log inundaria la consola.
        return;
    }

    if (strcmp(act, "ARM") == 0) {
        state_set_armed(true);
        ESP_LOGI(TAG, "ROVER ARMADO.");
        return;
    }

    if (strcmp(act, "DRIVE") == 0) {
        if (!state_is_armed()) return;           // se ignora en silencio si esta desarmado

        cJSON *lin = cJSON_GetObjectItem(root, "linear");
        cJSON *ang = cJSON_GetObjectItem(root, "angular");
        if (!cJSON_IsNumber(lin) || !cJSON_IsNumber(ang)) return;

        float linear  = (float)lin->valuedouble;
        float angular = (float)ang->valuedouble;

        mcpwm_motors_drive_differential(linear, angular);

        float steer = STEER_CENTER_DEG + (angular / 100.0f) * STEER_THROW_DEG;
        if (steer < STEER_CENTER_DEG - STEER_THROW_DEG) steer = STEER_CENTER_DEG - STEER_THROW_DEG;
        if (steer > STEER_CENTER_DEG + STEER_THROW_DEG) steer = STEER_CENTER_DEG + STEER_THROW_DEG;

        // pca9685_set_angle filtra los angulos repetidos, asi que en marcha
        // recta esto no genera trafico I2C.
        for (uint8_t ch = 0; ch < PCA9685_STEERING_CHANNELS; ch++) {
            pca9685_set_angle(ch, steer);
        }
        return;
    }

    if (strcmp(act, "TEST_MOTOR") == 0) {
        cJSON *idx = cJSON_GetObjectItem(root, "motor");
        cJSON *spd = cJSON_GetObjectItem(root, "speed");
        if (cJSON_IsNumber(idx) && cJSON_IsNumber(spd)) {
            mcpwm_motor_set_speed(idx->valueint, (float)spd->valuedouble);
        }
        return;
    }

    if (strcmp(act, "SERVO_SET") == 0) {
        cJSON *ch  = cJSON_GetObjectItem(root, "channel");
        cJSON *ang = cJSON_GetObjectItem(root, "angle");
        if (cJSON_IsNumber(ch) && cJSON_IsNumber(ang)) {
            pca9685_set_angle((uint8_t)ch->valueint, (float)ang->valuedouble);
        }
        return;
    }

    if (strcmp(act, "STEPPER_JOG") == 0) {
        cJSON *steps = cJSON_GetObjectItem(root, "steps");
        cJSON *spd   = cJSON_GetObjectItem(root, "speed");
        if (cJSON_IsNumber(steps)) {
            uint32_t hz = cJSON_IsNumber(spd) ? (uint32_t)spd->valueint : 0;
            stepper_drv_jog(steps->valueint, hz);
        }
        return;
    }

    ESP_LOGW(TAG, "Accion desconocida: %s", act);
}

// Parsea una linea completa y la despacha.
static void dispatch_line(const char *line)
{
    cJSON *root = cJSON_Parse(line);
    if (!root) {
        state_count_error();
        return;
    }
    handle_command(root);
    cJSON_Delete(root);
}

/* ------------------------------------------------------------------------ */
/* Generador de telemetria                                                   */
/* ------------------------------------------------------------------------ */

// Numero de clientes WebSocket vivos; 0 si el addon web esta desactivado.
static int link_ws_client_count(void);

/*
 * Serializa el estado completo en una linea JSON. Devuelve la longitud escrita.
 * Usa snprintf sobre un buffer del llamador: sin malloc en el lazo de 20 Hz.
 *
 * Todos los campos son medidas reales del rover. No hay lectura de bateria
 * porque no hay bateria instrumentada: el divisor del GPIO 9 no esta montado y
 * publicar el ruido de un pin al aire como "voltaje" seria un dato inventado.
 */
static int build_telemetry(char *out, size_t out_len)
{
    float speeds[NUM_MOTORS];
    mcpwm_motors_get_speeds(speeds);

    float servos[PCA9685_STEERING_CHANNELS];
    pca9685_get_angles(servos, PCA9685_STEERING_CHANNELS);

    char owner[OWNER_ID_LEN];
    portENTER_CRITICAL(&g_state_lock);
    bool     armed  = g_armed;
    uint32_t rx_ok  = g_rx_ok;
    uint32_t rx_err = g_rx_err;
    uint32_t seq    = g_last_seq;
    memcpy(owner, g_owner, sizeof(owner));
    portEXIT_CRITICAL(&g_state_lock);

    return snprintf(out, out_len,
        "{\"type\":\"telemetry\",\"seq\":%" PRIu32
        ",\"uptime\":%" PRIu32
        ",\"armed\":%s"
        ",\"owner\":\"%s\""
        ",\"motors\":[%.1f,%.1f,%.1f,%.1f,%.1f,%.1f]"
        ",\"servos\":[%.0f,%.0f,%.0f,%.0f,%.0f]"
        ",\"stepper_pos\":%" PRId32 ",\"stepper_moving\":%s"
        ",\"rx_ok\":%" PRIu32 ",\"rx_err\":%" PRIu32
        ",\"clients\":%d,\"heap\":%" PRIu32 "}\n",
        seq,
        (uint32_t)(esp_timer_get_time() / 1000000LL),
        armed ? "true" : "false",
        owner,
        speeds[0], speeds[1], speeds[2], speeds[3], speeds[4], speeds[5],
        servos[0], servos[1], servos[2], servos[3], servos[4],
        stepper_drv_get_position(),
        stepper_drv_is_moving() ? "true" : "false",
        rx_ok, rx_err,
        link_ws_client_count(),
        (uint32_t)esp_get_free_heap_size());
}

/* ------------------------------------------------------------------------ */
/* Failsafe                                                                  */
/* ------------------------------------------------------------------------ */

static void failsafe_task(void *arg)
{
    (void)arg;
    const int64_t timeout_us = (int64_t)ROVER_FAILSAFE_TIMEOUT_MS * 1000;

    // Sin esto, el primer ciclo ve un silencio de "todo el uptime" y dispara.
    state_mark_packet();

    while (1) {
        /*
         * Vigila el enlace siempre que haya hardware en movimiento, no solo
         * cuando el rover esta armado: un TEST_MOTOR desde el panel de
         * diagnostico mueve las ruedas con el rover desarmado, y si el cliente
         * se cierra a media prueba nadie mandaria el TEST_MOTOR de parada.
         */
        bool hardware_active = state_is_armed() ||
                               mcpwm_motors_any_active() ||
                               stepper_drv_is_moving();

        if (hardware_active && state_silence_us() > timeout_us) {
            ESP_LOGW(TAG, "FAILSAFE: sin trafico de ningun cliente por mas de %d ms. Parando.",
                     ROVER_FAILSAFE_TIMEOUT_MS);
            state_set_armed(false);
            rover_safe_stop();
            // Libera el mando: si el operario que lo tenia se desconecto, otro
            // cliente debe poder tomar el control sin reiniciar el rover.
            state_release_control();
            // Evita relanzar el aviso en bucle mientras el enlace siga caido.
            state_mark_packet();
        }

        vTaskDelay(pdMS_TO_TICKS(FAILSAFE_POLL_MS));
    }
}

/* ======================================================================== */
/* Transporte por cable: UART0                                              */
/* ======================================================================== */
#if ROVER_LINK_UART

#define LINK_UART        UART_NUM_0
#define UART_RX_BUF_LEN  2048
#define UART_TX_BUF_LEN  2048

static void uart_rx_task(void *arg)
{
    (void)arg;

    static char line[MAX_LINE_LEN];
    static uint8_t chunk[128];
    size_t line_len = 0;
    bool   overflow = false;

    while (1) {
        /*
         * uart_read_bytes se bloquea en el driver hasta que hay datos o vence
         * el plazo, y el driver vacia el FIFO por interrupcion hacia un anillo
         * de 2 KB. La version anterior hacia getchar() + vTaskDelay(10 ms) en
         * EOF: a 115200 baudios, esos 10 ms son ~115 bytes y el FIFO hardware
         * de UART0 solo tiene 128, asi que se perdian bytes en mitad de una
         * linea. De ahi el desfase y el congelamiento.
         */
        int n = uart_read_bytes(LINK_UART, chunk, sizeof(chunk), pdMS_TO_TICKS(20));
        if (n <= 0) continue;

        for (int i = 0; i < n; i++) {
            char c = (char)chunk[i];

            if (c == '\n' || c == '\r') {
                if (overflow) {
                    // Linea truncada: se descarta entera en vez de despachar basura.
                    state_count_error();
                    overflow = false;
                } else if (line_len > 0) {
                    line[line_len] = '\0';
                    dispatch_line(line);
                }
                line_len = 0;
                continue;
            }

            if (line_len < sizeof(line) - 1) {
                line[line_len++] = c;
            } else {
                // Marca desbordamiento y sigue consumiendo hasta el fin de linea,
                // para resincronizar en la siguiente trama.
                overflow = true;
                line_len = 0;
            }
        }
    }
}

static void uart_telemetry_task(void *arg)
{
    (void)arg;
    static char buf[TELEMETRY_BUF_LEN];
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        int len = build_telemetry(buf, sizeof(buf));
        if (len > 0) {
            if (len > (int)sizeof(buf) - 1) len = (int)sizeof(buf) - 1;
            // Escritura por el anillo TX del driver: no hace espera activa
            // sobre el FIFO como printf().
            uart_write_bytes(LINK_UART, buf, len);
        }
        // Cadencia fija: vTaskDelayUntil no acumula la deriva del tiempo de trabajo.
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
    }
}

static esp_err_t uart_link_start(void)
{
    esp_err_t ret = uart_driver_install(LINK_UART, UART_RX_BUF_LEN, UART_TX_BUF_LEN,
                                        0, NULL, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "No se pudo instalar el driver de UART0: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Enlace por cable listo. UART0 a %d baudios, %d Hz de telemetria.",
             CONFIG_ESP_CONSOLE_UART_BAUDRATE, ROVER_TELEMETRY_HZ);

    xTaskCreatePinnedToCore(uart_rx_task,        "link_uart_rx",  4096, NULL, 6, NULL, 0);
    xTaskCreatePinnedToCore(uart_telemetry_task, "link_uart_tlm", 4096, NULL, 4, NULL, 0);
    return ESP_OK;
}

#endif // ROVER_LINK_UART

/* ======================================================================== */
/* Addon web: SoftAP + servidor HTTP + WebSocket                            */
/* ======================================================================== */
#if ROVER_LINK_HTTP

/*
 * El panel va embebido en el binario (EMBED_FILES en CMakeLists.txt), asi que
 * un solo `idf.py flash` deja la interfaz servida en http://192.168.4.1/ sin
 * particion de archivos ni paso de flasheo extra. Los simbolos los genera el
 * sistema de compilacion a partir del nombre de cada fichero.
 */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t app_js_start[]     asm("_binary_app_js_start");
extern const uint8_t app_js_end[]       asm("_binary_app_js_end");
extern const uint8_t style_css_start[]  asm("_binary_style_css_start");
extern const uint8_t style_css_end[]    asm("_binary_style_css_end");

typedef struct {
    const char    *uri;
    const char    *content_type;
    const uint8_t *start;
    const uint8_t *end;
} static_asset_t;

static const static_asset_t g_assets[] = {
    { "/",          "text/html",       index_html_start, index_html_end },
    { "/app.js",    "text/javascript", app_js_start,     app_js_end     },
    { "/style.css", "text/css",        style_css_start,  style_css_end  },
};

static httpd_handle_t g_server = NULL;

static void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *netif = esp_netif_create_default_wifi_ap();

    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip,      192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw,      192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(netif));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid           = ROVER_AP_SSID,
            .ssid_len       = strlen(ROVER_AP_SSID),
            .channel        = 1,
            .password       = ROVER_AP_PASS,
            .max_connection = ROVER_AP_MAX_CONN,
            .authmode       = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // El rover manda a 20 Hz: el ahorro de energia de Wi-Fi solo anadiria jitter.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGI(TAG, "SoftAP levantado. SSID: %s | Panel en http://192.168.4.1/", ROVER_AP_SSID);
}

static esp_err_t static_get_handler(httpd_req_t *req)
{
    const static_asset_t *asset = (const static_asset_t *)req->user_ctx;

    httpd_resp_set_type(req, asset->content_type);
    // Sin cache: si se reflashea con una UI nueva, el navegador no debe servir
    // la anterior desde su cache.
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    return httpd_resp_send(req, (const char *)asset->start,
                           (ssize_t)(asset->end - asset->start));
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Cliente WebSocket conectado (fd %d).", httpd_req_to_sockfd(req));
        state_mark_packet();
        return ESP_OK;
    }

    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &frame, 0);
    if (ret != ESP_OK) return ret;
    if (frame.len == 0 || frame.len > MAX_LINE_LEN) return ESP_OK;

    uint8_t *buf = calloc(1, frame.len + 1);
    if (!buf) return ESP_ERR_NO_MEM;

    frame.payload = buf;
    ret = httpd_ws_recv_frame(req, &frame, frame.len);
    if (ret == ESP_OK) {
        // Un cliente puede agrupar varias tramas JSON separadas por salto de linea.
        char *save = NULL;
        for (char *line = strtok_r((char *)buf, "\n", &save);
             line != NULL;
             line = strtok_r(NULL, "\n", &save)) {
            if (*line) dispatch_line(line);
        }
    }
    free(buf);
    return ESP_OK;
}

/*
 * Difunde a todos los clientes WebSocket. httpd_get_client_list devuelve solo
 * los sockets vivos, asi que las desconexiones se limpian solas: no hace falta
 * cachear un fd que se quede colgado apuntando a un cliente muerto.
 */
static void ws_broadcast(const char *payload, size_t len)
{
    if (!g_server) return;

    int    fds[ROVER_WS_MAX_CLIENTS];
    size_t count = ROVER_WS_MAX_CLIENTS;
    if (httpd_get_client_list(g_server, &count, fds) != ESP_OK) return;

    httpd_ws_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.payload = (uint8_t *)payload;
    frame.len     = len;
    frame.type    = HTTPD_WS_TYPE_TEXT;

    for (size_t i = 0; i < count; i++) {
        if (httpd_ws_get_fd_info(g_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(g_server, fds[i], &frame);
        }
    }
}

static int link_ws_client_count(void)
{
    if (!g_server) return 0;

    int    fds[ROVER_WS_MAX_CLIENTS];
    size_t count = ROVER_WS_MAX_CLIENTS;
    if (httpd_get_client_list(g_server, &count, fds) != ESP_OK) return 0;

    int ws = 0;
    for (size_t i = 0; i < count; i++) {
        if (httpd_ws_get_fd_info(g_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) ws++;
    }
    return ws;
}

static void ws_telemetry_task(void *arg)
{
    (void)arg;
    static char buf[TELEMETRY_BUF_LEN];
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        int len = build_telemetry(buf, sizeof(buf));
        if (len > 0) {
            if (len > (int)sizeof(buf) - 1) len = (int)sizeof(buf) - 1;
            ws_broadcast(buf, (size_t)len);
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
    }
}

static esp_err_t http_link_start(void)
{
    wifi_init_softap();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = ROVER_HTTP_PORT;
    config.ctrl_port        = 32768;
    config.max_uri_handlers = 8;
    // Handlers + WebSockets abiertos a la vez.
    config.max_open_sockets = ROVER_WS_MAX_CLIENTS + 2;
    config.lru_purge_enable = true;

    esp_err_t ret = httpd_start(&g_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start fallo: %s", esp_err_to_name(ret));
        return ret;
    }

    for (size_t i = 0; i < sizeof(g_assets) / sizeof(g_assets[0]); i++) {
        httpd_uri_t uri = {
            .uri      = g_assets[i].uri,
            .method   = HTTP_GET,
            .handler  = static_get_handler,
            .user_ctx = (void *)&g_assets[i],
        };
        ESP_ERROR_CHECK(httpd_register_uri_handler(g_server, &uri));
    }

    httpd_uri_t ws_uri = {
        .uri          = "/ws",
        .method       = HTTP_GET,
        .handler      = ws_handler,
        .user_ctx     = NULL,
        .is_websocket = true,
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(g_server, &ws_uri));

    xTaskCreatePinnedToCore(ws_telemetry_task, "link_ws_tlm", 4096, NULL, 4, NULL, 0);

    ESP_LOGI(TAG, "Servidor web listo en el puerto %d (UI + /ws).", ROVER_HTTP_PORT);
    return ESP_OK;
}

#else  // ROVER_LINK_HTTP

static int link_ws_client_count(void) { return 0; }

#endif // ROVER_LINK_HTTP

/* ------------------------------------------------------------------------ */
/* API publica                                                               */
/* ------------------------------------------------------------------------ */

esp_err_t rover_link_init(void)
{
#if !ROVER_LINK_UART && !ROVER_LINK_HTTP
#error "Hay que habilitar al menos un transporte en rover_link.h"
#endif

#if ROVER_LINK_UART
    ESP_ERROR_CHECK(uart_link_start());
#endif

#if ROVER_LINK_HTTP
    /*
     * El addon web se levanta despues del enlace por cable y sin ESP_ERROR_CHECK:
     * si el Wi-Fi falla, el rover tiene que seguir gobernable por USB.
     */
    if (http_link_start() != ESP_OK) {
        ESP_LOGE(TAG, "El addon web no arranco. El enlace por cable sigue operativo.");
    }
#endif

    // Un unico failsafe para todos los transportes.
    xTaskCreatePinnedToCore(failsafe_task, "link_failsafe", 3072, NULL, 7, NULL, 0);

#if ROVER_LINK_UART
    /*
     * A partir de aqui los ESP_LOG comparten UART con la telemetria y podrian
     * partir una linea JSON. Se bajan a WARN: los mensajes de arranque ya se
     * emitieron y siguen visibles los avisos que importan (failsafe, I2C caido).
     * El panel ademas descarta toda linea que no empiece por '{'.
     */
    esp_log_level_set("*", ESP_LOG_WARN);
#endif

    return ESP_OK;
}

bool rover_link_is_armed(void)
{
    return state_is_armed();
}
