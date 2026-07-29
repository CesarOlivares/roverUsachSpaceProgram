#include "websocket_server.h"
#include "mcpwm_motors.h"
#include "stepper_drv.h"
#include "pca9685_servo.h"
#include "telemetry_adc.h"

#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "cJSON.h"

static const char *TAG = "WS_SERVER";

#define WIFI_SSID "Rover_6x6_AP"
#define WIFI_PASS "Rover6x6Secure"

static httpd_handle_t g_server = NULL;
static int g_ws_fd = -1;
static bool g_armed = false;
static int64_t g_last_packet_time_us = 0;

static void wifi_init_softap(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *netif = esp_netif_create_default_wifi_ap();
    
    // Set static IP 192.168.4.1
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(netif);
    esp_netif_set_ip_info(netif, &ip_info);
    esp_netif_dhcps_start(netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = 1,
            .password = WIFI_PASS,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi SoftAP started. SSID: %s | IP: 192.168.4.1", WIFI_SSID);
}

static void handle_ws_json_command(cJSON *root) {
    cJSON *action = cJSON_GetObjectItem(root, "action");
    if (!cJSON_IsString(action)) return;

    const char *act_str = action->valuestring;
    g_last_packet_time_us = esp_timer_get_time();

    if (strcmp(act_str, "ARM") == 0) {
        g_armed = true;
        ESP_LOGI(TAG, "ROVER SYSTEM ARMED.");
    } 
    else if (strcmp(act_str, "DISARM") == 0) {
        g_armed = false;
        mcpwm_motors_stop_all();
        stepper_drv_stop();
        ESP_LOGI(TAG, "ROVER SYSTEM DISARMED.");
    }
    else if (strcmp(act_str, "DRIVE") == 0) {
        if (g_armed) {
            cJSON *lin = cJSON_GetObjectItem(root, "linear");
            cJSON *ang = cJSON_GetObjectItem(root, "angular");
            if (cJSON_IsNumber(lin) && cJSON_IsNumber(ang)) {
                float linear_val = (float)lin->valuedouble;
                float angular_val = (float)ang->valuedouble;

                // 1. Tracción Cinemática Diferencial (Motores M1..M6)
                mcpwm_motors_drive_differential(linear_val, angular_val);

                // 2. Geometría de Dirección por Servos (45° Izquierda -> 90° Centro -> 135° Derecha)
                float servo_angle = 90.0f + (angular_val / 100.0f) * 45.0f;
                if (servo_angle < 45.0f) servo_angle = 45.0f;
                if (servo_angle > 135.0f) servo_angle = 135.0f;

                for (uint8_t ch = 0; ch < 5; ch++) {
                    pca9685_set_angle(ch, servo_angle);
                }
            }
        }
    }
    else if (strcmp(act_str, "TEST_MOTOR") == 0) {
        cJSON *m_idx = cJSON_GetObjectItem(root, "motor");
        cJSON *spd = cJSON_GetObjectItem(root, "speed");
        if (cJSON_IsNumber(m_idx) && cJSON_IsNumber(spd)) {
            mcpwm_motor_set_speed(m_idx->valueint, (float)spd->valuedouble);
        }
    }
    else if (strcmp(act_str, "SERVO_SET") == 0) {
        cJSON *ch = cJSON_GetObjectItem(root, "channel");
        cJSON *ang = cJSON_GetObjectItem(root, "angle");
        if (cJSON_IsNumber(ch) && cJSON_IsNumber(ang)) {
            pca9685_set_angle((uint8_t)ch->valueint, (float)ang->valuedouble);
        }
    }
    else if (strcmp(act_str, "STEPPER_JOG") == 0) {
        cJSON *st = cJSON_GetObjectItem(root, "steps");
        cJSON *spd = cJSON_GetObjectItem(root, "speed");
        if (cJSON_IsNumber(st)) {
            int speed_hz = cJSON_IsNumber(spd) ? spd->valueint : 1000;
            stepper_drv_jog(st->valueint, speed_hz);
        }
    }
}

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "Handshake requested, WebSocket connection established");
        g_ws_fd = httpd_req_to_sockfd(req);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) return ret;

    if (ws_pkt.len) {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) return ESP_ERR_NO_MEM;
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret == ESP_OK) {
            cJSON *root = cJSON_Parse((char *)buf);
            if (root) {
                handle_ws_json_command(root);
                cJSON_Delete(root);
            }
        }
        free(buf);
    }
    return ESP_OK;
}

static const httpd_uri_t ws_uri = {
    .uri        = "/ws",
    .method     = HTTP_GET,
    .handler    = ws_handler,
    .user_ctx   = NULL,
    .is_websocket = true
};

static void telemetry_broadcast_task(void *pvParameters) {
    while (1) {
        if (g_server && g_ws_fd >= 0) {
            float batt_v = 0.0f;
            uint32_t raw_mv = 0;
            telemetry_adc_read(&batt_v, &raw_mv);

            float speeds[NUM_MOTORS];
            mcpwm_motors_get_speeds(speeds);

            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "type", "telemetry");
            cJSON_AddNumberToObject(root, "battery_v", batt_v);
            cJSON_AddNumberToObject(root, "raw_mv", raw_mv);
            cJSON_AddNumberToObject(root, "uptime", esp_timer_get_time() / 1000000ULL);
            cJSON_AddBoolToObject(root, "armed", g_armed);

            cJSON *m_arr = cJSON_CreateFloatArray(speeds, NUM_MOTORS);
            cJSON_AddItemToObject(root, "motors", m_arr);
            cJSON_AddNumberToObject(root, "stepper_pos", stepper_drv_get_position());
            cJSON_AddBoolToObject(root, "stepper_moving", stepper_drv_is_moving());

            char *json_str = cJSON_PrintUnformatted(root);
            cJSON_Delete(root);

            if (json_str) {
                httpd_ws_frame_t ws_pkt;
                memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
                ws_pkt.payload = (uint8_t *)json_str;
                ws_pkt.len = strlen(json_str);
                ws_pkt.type = HTTPD_WS_TYPE_TEXT;

                httpd_ws_send_frame_async(g_server, g_ws_fd, &ws_pkt);
                free(json_str);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50)); // 20 Hz Broadcast
    }
}

static void failsafe_watchdog_task(void *pvParameters) {
    while (1) {
        int64_t now = esp_timer_get_time();
        if (g_armed && (now - g_last_packet_time_us) > (FAILSAFE_TIMEOUT_MS * 1000LL)) {
            ESP_LOGW(TAG, "FAILSAFE WATCHDOG TRIGGERED! (> 400ms packet loss). Stopping motors.");
            g_armed = false;
            mcpwm_motors_stop_all();
            stepper_drv_stop();
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

esp_err_t websocket_server_init(void) {
    wifi_init_softap();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WS_SERVER_PORT;
    config.ctrl_port = 32768;

    ESP_LOGI(TAG, "Starting WebSocket HTTP Server on port %d...", WS_SERVER_PORT);
    if (httpd_start(&g_server, &config) == ESP_OK) {
        httpd_register_uri_handler(g_server, &ws_uri);

        xTaskCreatePinnedToCore(telemetry_broadcast_task, "ws_telem_task", 4096, NULL, 4, NULL, 0);
        xTaskCreatePinnedToCore(failsafe_watchdog_task, "ws_failsafe_task", 2048, NULL, 5, NULL, 0);
        return ESP_OK;
    }

    return ESP_FAIL;
}

bool websocket_is_armed(void) {
    return g_armed;
}

void websocket_set_armed(bool armed) {
    g_armed = armed;
}
