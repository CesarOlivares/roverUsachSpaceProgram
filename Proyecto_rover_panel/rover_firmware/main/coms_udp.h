#ifndef COMS_UDP_H
#define COMS_UDP_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UDP_SERVER_PORT 5005
#define UDP_CLIENT_PORT 5006
#define CMD_MAGIC       0x5256 // 'RV'
#define TELEM_MAGIC     0x544D // 'TM'

#pragma pack(push, 1)
typedef struct {
    uint16_t magic;           // 0x5256
    uint32_t sequence;        // Packet sequence counter
    float linear_speed;       // -100.0 to +100.0
    float angular_speed;      // -100.0 to +100.0
    float servo_angles[16];   // Servo angles 0..180
    int32_t stepper_target;   // Target step position
    uint32_t stepper_speed;   // Stepper Hz
    uint8_t emergency_stop;   // 1 = E-Stop active, 0 = Normal
    uint8_t reserved[3];
} rover_command_pkt_t;

typedef struct {
    uint16_t magic;           // 0x544D
    uint32_t sequence;        // Packet sequence counter
    float battery_voltage;    // Volts
    float motor_speeds[6];    // Motor duties -100..+100
    int32_t stepper_position; // Stepper current step
    uint8_t stepper_moving;   // 1 = moving, 0 = idle
    uint32_t uptime_seconds;  // Uptime in sec
    int8_t rssi;              // WiFi signal strength dBm
    uint8_t reserved[2];
} rover_telemetry_pkt_t;
#pragma pack(pop)

/**
 * @brief Initialize Wi-Fi in SoftAP mode ("Rover_AP") and start the UDP Socket task.
 * @return esp_err_t ESP_OK on success
 */
esp_err_t coms_udp_init(void);

/**
 * @brief Check if GCS heartbeat is active.
 * @return true if connected within watchdog timeout, false otherwise
 */
bool coms_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif // COMS_UDP_H
