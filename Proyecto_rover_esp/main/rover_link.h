#ifndef ROVER_LINK_H
#define ROVER_LINK_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Enlace de mando y telemetria del rover.
 *
 * Dos transportes intercambiables que comparten el mismo dispatcher JSON y el
 * mismo generador de telemetria:
 *
 *   ROVER_LINK_WIRED = 1 -> UART0 por USB, una linea JSON por trama (115200 8N1)
 *   ROVER_LINK_WIRED = 0 -> SoftAP Wi-Fi + WebSocket en ws://192.168.4.1:8080/ws
 */

// 1 = Modo cable (USB serie). 0 = Modo definitivo (Wi-Fi AP + WebSocket).
#define ROVER_LINK_WIRED 1

#define ROVER_WS_PORT           8080

// Sin trafico del panel durante este tiempo, el failsafe corta la potencia.
// El panel emite a 20 Hz (50 ms), asi que hay 8x de margen.
#define ROVER_FAILSAFE_TIMEOUT_MS 400

// Cadencia de telemetria hacia el panel.
#define ROVER_TELEMETRY_HZ        20

/**
 * @brief Levanta el transporte activo y sus tareas (RX, telemetria, failsafe).
 *
 * Debe llamarse despues de inicializar motores, stepper, servos y ADC.
 */
esp_err_t rover_link_init(void);

/** @brief true si el rover esta armado (conduccion habilitada). */
bool rover_link_is_armed(void);

#ifdef __cplusplus
}
#endif

#endif // ROVER_LINK_H
