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
 * Los dos transportes son independientes y pueden convivir: comparten el mismo
 * dispatcher JSON, el mismo generador de telemetria y el mismo failsafe.
 *
 *   UART  linea JSON por UART0 (USB, 115200 8N1). Es el camino de siempre.
 *   HTTP  addon: SoftAP + servidor web que sirve el panel y expone /ws.
 *
 * Poner cualquiera a 0 lo compila fuera sin tocar el otro.
 */
#define ROVER_LINK_UART 1
#define ROVER_LINK_HTTP 1

// SoftAP del addon web. La UI queda en http://192.168.4.1/
#define ROVER_AP_SSID       "Rover_6x6_AP"
#define ROVER_AP_PASS       "Rover6x6Secure"
#define ROVER_AP_MAX_CONN   4
#define ROVER_HTTP_PORT     80

// Maximo de clientes WebSocket a los que se difunde la telemetria.
#define ROVER_WS_MAX_CLIENTS 4

/*
 * Sin trafico de NINGUN operador durante este tiempo, el failsafe corta la
 * potencia. Los clientes emiten a 20 Hz (50 ms), asi que hay 8x de margen.
 *
 * Nota: el temporizador es comun a los dos transportes. Si el panel por cable
 * sigue latiendo, un cliente web que se caiga no dispara el failsafe, porque
 * sigue habiendo un operador vivo al mando.
 */
#define ROVER_FAILSAFE_TIMEOUT_MS 400

// Cadencia de telemetria hacia los clientes.
#define ROVER_TELEMETRY_HZ 20

/**
 * @brief Levanta los transportes habilitados y sus tareas (RX, telemetria, failsafe).
 *
 * Debe llamarse despues de inicializar motores, stepper y servos.
 */
esp_err_t rover_link_init(void);

/** @brief true si el rover esta armado (conduccion habilitada). */
bool rover_link_is_armed(void);

#ifdef __cplusplus
}
#endif

#endif // ROVER_LINK_H
