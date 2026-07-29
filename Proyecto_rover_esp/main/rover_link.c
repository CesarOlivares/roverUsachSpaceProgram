#include "rover_link.h"
#include "mcpwm_motors.h"
#include "stepper_drv.h"
#include "pca9685_servo.h"
#include "telemetry_adc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

#if ROVER_LINK_WIRED
#include "driver/uart.h"
#else
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
#define TELEMETRY_BUF_LEN     320

// Geometria de direccion: 45 grados izq <- 90 centro -> 135 der.
#define STEER_CENTER_DEG      90.0f
#define STEER_THROW_DEG       45.0f

/* ------------------------------------------------------------------------ */
/* Estado compartido                                                         */
/* ------------------------------------------------------------------------ */

/*
 * g_armed y g_last_packet_us los tocan la tarea de RX, la de failsafe y la de
 * telemetria, repartidas entre los dos nucleos. g_last_packet_us son 64 bits,
 * asi que ni siquiera su lectura es atomica en una MCU de 32 bits: hace falta
 * seccion critica, no solo volatile.
 */
static portMUX_TYPE g_state_lock = portMUX_INITIALIZER_UNLOCKED;
static bool     g_armed          = false;
static int64_t  g_last_packet_us = 0;
static uint32_t g_rx_ok          = 0;   // tramas JSON aceptadas
static uint32_t g_rx_err         = 0;   // lineas descartadas (JSON invalido / desbordadas)
static uint32_t g_last_seq       = 0;   // secuencia del panel, se devuelve para medir RTT

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
        portENTER_CRITICAL(&g_state_lock);
        g_rx_err++;
        portEXIT_CRITICAL(&g_state_lock);
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

    if (strcmp(act, "ARM") == 0) {
        state_set_armed(true);
        ESP_LOGI(TAG, "ROVER ARMADO.");
        return;
    }

    if (strcmp(act, "DISARM") == 0 || strcmp(act, "STOP") == 0) {
        state_set_armed(false);
        rover_safe_stop();
        ESP_LOGI(TAG, "ROVER DESARMADO.");
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

// Parsea una linea completa y la despacha. Devuelve false si no era JSON valido.
static bool dispatch_line(const char *line)
{
    cJSON *root = cJSON_Parse(line);
    if (!root) {
        portENTER_CRITICAL(&g_state_lock);
        g_rx_err++;
        portEXIT_CRITICAL(&g_state_lock);
        return false;
    }
    handle_command(root);
    cJSON_Delete(root);
    return true;
}

/* ------------------------------------------------------------------------ */
/* Generador de telemetria                                                   */
/* ------------------------------------------------------------------------ */

// Serializa el estado completo en una linea JSON. Devuelve la longitud escrita.
// Usa snprintf sobre un buffer del llamador: sin malloc en el lazo de 20 Hz.
static int build_telemetry(char *out, size_t out_len)
{
    float speeds[NUM_MOTORS];
    mcpwm_motors_get_speeds(speeds);

    float    batt_v = 0.0f;
    uint32_t raw_mv = 0;
    telemetry_adc_read(&batt_v, &raw_mv);

    portENTER_CRITICAL(&g_state_lock);
    bool     armed   = g_armed;
    uint32_t rx_ok   = g_rx_ok;
    uint32_t rx_err  = g_rx_err;
    uint32_t seq     = g_last_seq;
    portEXIT_CRITICAL(&g_state_lock);

    return snprintf(out, out_len,
        "{\"type\":\"telemetry\",\"seq\":%" PRIu32
        ",\"battery_v\":%.2f,\"raw_mv\":%" PRIu32
        ",\"uptime\":%" PRIu32
        ",\"armed\":%s"
        ",\"motors\":[%.1f,%.1f,%.1f,%.1f,%.1f,%.1f]"
        ",\"stepper_pos\":%" PRId32 ",\"stepper_moving\":%s"
        ",\"rx_ok\":%" PRIu32 ",\"rx_err\":%" PRIu32 "}\n",
        seq,
        batt_v, raw_mv,
        (uint32_t)(esp_timer_get_time() / 1000000LL),
        armed ? "true" : "false",
        speeds[0], speeds[1], speeds[2], speeds[3], speeds[4], speeds[5],
        stepper_drv_get_position(),
        stepper_drv_is_moving() ? "true" : "false",
        rx_ok, rx_err);
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
         * diagnostico mueve las ruedas con el rover desarmado, y si el panel
         * se cierra a media prueba nadie mandaria el TEST_MOTOR de parada.
         */
        bool hardware_active = state_is_armed() ||
                               mcpwm_motors_any_active() ||
                               stepper_drv_is_moving();

        if (hardware_active && state_silence_us() > timeout_us) {
            ESP_LOGW(TAG, "FAILSAFE: sin trafico del panel por mas de %d ms. Parando.",
                     ROVER_FAILSAFE_TIMEOUT_MS);
            state_set_armed(false);
            rover_safe_stop();
            // Evita relanzar el aviso en bucle mientras el enlace siga caido.
            state_mark_packet();
        }

        vTaskDelay(pdMS_TO_TICKS(FAILSAFE_POLL_MS));
    }
}

/* ======================================================================== */
/* Transporte por cable: UART0                                              */
/* ======================================================================== */
#if ROVER_LINK_WIRED

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
                    portENTER_CRITICAL(&g_state_lock);
                    g_rx_err++;
                    portEXIT_CRITICAL(&g_state_lock);
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

static esp_err_t wired_link_start(void)
{
    esp_err_t ret = uart_driver_install(LINK_UART, UART_RX_BUF_LEN, UART_TX_BUF_LEN,
                                        0, NULL, 0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "No se pudo instalar el driver de UART0: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Enlace por cable listo. UART0 a %d baudios, %d Hz de telemetria, "
                  "una linea JSON por trama.",
             CONFIG_ESP_CONSOLE_UART_BAUDRATE, ROVER_TELEMETRY_HZ);

    xTaskCreatePinnedToCore(uart_rx_task,        "link_rx",    4096, NULL, 6, NULL, 0);
    xTaskCreatePinnedToCore(uart_telemetry_task, "link_tlm",   4096, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(failsafe_task,       "link_fsafe", 3072, NULL, 7, NULL, 0);

    /*
     * A partir de aqui los ESP_LOG comparten UART con la telemetria y podrian
     * partir una linea JSON. Se bajan a WARN: los mensajes de arranque ya se
     * emitieron y siguen visibles los avisos que importan (failsafe, I2C caido).
     * El panel ademas descarta toda linea que no empiece por '{'.
     */
    esp_log_level_set("*", ESP_LOG_WARN);
    return ESP_OK;
}

/* ======================================================================== */
/* Transporte inalambrico: SoftAP + WebSocket                               */
/* ======================================================================== */
#else

#define WIFI_SSID  "Rover_6x6_AP"
#define WIFI_PASS  "Rover6x6Secure"

static httpd_handle_t   g_server = NULL;
static volatile int     g_ws_fd  = -1;  // -1 = sin cliente

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
            .ssid           = WIFI_SSID,
            .ssid_len       = strlen(WIFI_SSID),
            .channel        = 1,
            .password       = WIFI_PASS,
            .max_connection = 4,
            .authmode       = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP levantado. SSID: %s | IP: 192.168.4.1", WIFI_SSID);
}

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        g_ws_fd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "Cliente WebSocket conectado (fd %d).", g_ws_fd);
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
        dispatch_line((char *)buf);
    }
    free(buf);
    return ESP_OK;
}

static const httpd_uri_t ws_uri = {
    .uri          = "/ws",
    .method       = HTTP_GET,
    .handler      = ws_handler,
    .user_ctx     = NULL,
    .is_websocket = true,
};

static void ws_telemetry_task(void *arg)
{
    (void)arg;
    static char buf[TELEMETRY_BUF_LEN];
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        int fd = g_ws_fd;
        if (g_server && fd >= 0) {
            int len = build_telemetry(buf, sizeof(buf));
            if (len > 0) {
                httpd_ws_frame_t frame;
                memset(&frame, 0, sizeof(frame));
                frame.payload = (uint8_t *)buf;
                frame.len     = (size_t)len;
                frame.type    = HTTPD_WS_TYPE_TEXT;

                /*
                 * Si el cliente se fue, el envio falla. Hay que soltar el fd:
                 * de lo contrario la tarea seguiria emitiendo eternamente hacia
                 * un socket muerto y el failsafe nunca veria la desconexion.
                 */
                if (httpd_ws_send_frame_async(g_server, fd, &frame) != ESP_OK) {
                    ESP_LOGW(TAG, "Cliente WebSocket %d perdido.", fd);
                    g_ws_fd = -1;
                }
            }
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
    }
}

static esp_err_t wireless_link_start(void)
{
    wifi_init_softap();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = ROVER_WS_PORT;
    config.ctrl_port   = 32768;

    ESP_LOGI(TAG, "Arrancando servidor WebSocket en el puerto %d...", ROVER_WS_PORT);
    esp_err_t ret = httpd_start(&g_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start fallo: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_ERROR_CHECK(httpd_register_uri_handler(g_server, &ws_uri));

    xTaskCreatePinnedToCore(ws_telemetry_task, "link_tlm",   4096, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(failsafe_task,     "link_fsafe", 3072, NULL, 7, NULL, 0);
    return ESP_OK;
}

#endif // ROVER_LINK_WIRED

/* ------------------------------------------------------------------------ */
/* API publica                                                               */
/* ------------------------------------------------------------------------ */

esp_err_t rover_link_init(void)
{
#if ROVER_LINK_WIRED
    return wired_link_start();
#else
    return wireless_link_start();
#endif
}

bool rover_link_is_armed(void)
{
    return state_is_armed();
}
