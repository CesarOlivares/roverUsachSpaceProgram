#include "coms_udp.h"
#include "motors.h"
#include "stepper.h"
#include "pca9685.h"
#include "telemetry.h"

#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"

static const char *TAG = "COMS_UDP";

#define WIFI_SSID       "ROVER_6X6_AP"
#define WIFI_PASS       "Rover6x6Secure"
#define FAILSAFE_TIMEOUT_MS 1500

static int g_udp_socket = -1;
static struct sockaddr_in g_gcs_addr;
static socklen_t g_addr_len = sizeof(g_gcs_addr);
static bool g_gcs_connected = false;
static int64_t g_last_cmd_time_us = 0;
static uint32_t g_telemetry_seq = 0;

static void wifi_init_softap(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

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

    ESP_LOGI(TAG, "Wi-Fi SoftAP started. SSID: %s, Pass: %s, IP: 192.168.4.1", WIFI_SSID, WIFI_PASS);
}

static void udp_server_task(void *pvParameters) {
    char rx_buffer[256];
    struct sockaddr_in server_addr;

    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(UDP_SERVER_PORT);

    g_udp_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (g_udp_socket < 0) {
        ESP_LOGE(TAG, "Unable to create UDP socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000; // 100ms
    setsockopt(g_udp_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (bind(g_udp_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        close(g_udp_socket);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UDP Socket listening on port %d...", UDP_SERVER_PORT);

    while (1) {
        int len = recvfrom(g_udp_socket, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&g_gcs_addr, &g_addr_len);

        int64_t now_us = esp_timer_get_time();

        if (len == sizeof(rover_command_pkt_t)) {
            rover_command_pkt_t *cmd = (rover_command_pkt_t *)rx_buffer;
            if (cmd->magic == CMD_MAGIC) {
                g_last_cmd_time_us = now_us;
                g_gcs_connected = true;

                // Handle E-STOP
                if (cmd->emergency_stop) {
                    motors_stop_all();
                    stepper_stop();
                    ESP_LOGW(TAG, "EMERGENCY STOP RECEIVED!");
                } else {
                    // Differential drive motor speed control
                    motors_drive_differential(cmd->linear_speed, cmd->angular_speed);

                    // Servo angles
                    for (int i = 0; i < 16; i++) {
                        if (cmd->servo_angles[i] >= 0.0f) {
                            pca9685_set_servo_angle(i, cmd->servo_angles[i]);
                        }
                    }

                    // Stepper command
                    if (cmd->stepper_speed > 0) {
                        stepper_move_to(cmd->stepper_target, cmd->stepper_speed);
                    }
                }
            }
        }

        // Failsafe Watchdog: stop motors if no packet received within timeout
        if (g_gcs_connected && (now_us - g_last_cmd_time_us) > (FAILSAFE_TIMEOUT_MS * 1000LL)) {
            ESP_LOGW(TAG, "Failsafe triggered! Lost connection to GCS. Stopping motors.");
            motors_stop_all();
            stepper_stop();
            g_gcs_connected = false;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void udp_telemetry_task(void *pvParameters) {
    rover_telemetry_pkt_t telem_pkt;

    while (1) {
        if (g_gcs_connected && g_udp_socket >= 0) {
            memset(&telem_pkt, 0, sizeof(telem_pkt));
            telem_pkt.magic = TELEM_MAGIC;
            telem_pkt.sequence = g_telemetry_seq++;

            // Read telemetry data
            telemetry_data_t t_data;
            if (telemetry_read(&t_data) == ESP_OK) {
                telem_pkt.battery_voltage = t_data.battery_voltage;
                telem_pkt.uptime_seconds = t_data.uptime_seconds;
            }

            // Stepper status
            stepper_status_t s_status;
            stepper_get_status(&s_status);
            telem_pkt.stepper_position = s_status.current_position;
            telem_pkt.stepper_moving = s_status.is_moving ? 1 : 0;

            // RSSI
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
                telem_pkt.rssi = ap_info.rssi;
            } else {
                telem_pkt.rssi = -50; // AP default nominal
            }

            // Target GCS port 5006
            struct sockaddr_in dest_addr = g_gcs_addr;
            dest_addr.sin_port = htons(UDP_CLIENT_PORT);

            sendto(g_udp_socket, &telem_pkt, sizeof(telem_pkt), 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        }

        vTaskDelay(pdMS_TO_TICKS(50)); // 20 Hz Telemetry broadcast
    }
}

esp_err_t coms_udp_init(void) {
    wifi_init_softap();
    xTaskCreatePinnedToCore(udp_server_task, "udp_server_task", 4096, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(udp_telemetry_task, "udp_telemetry_task", 3072, NULL, 3, NULL, 0);
    return ESP_OK;
}

bool coms_is_connected(void) {
    return g_gcs_connected;
}
