#pragma once

/*
 * Plantilla de credenciales Wi-Fi. Copiala como Secrets.h (que git ignora) y
 * cambia las claves: este archivo es publico.
 *
 *  - ROVER_AP_*:  red propia que levanta el rover (punto de acceso). La clave
 *                 WPA2 necesita al menos 8 caracteres.
 *  - ROVER_STA_*: red existente a la que el rover intenta unirse al arrancar
 *                 la primera vez. Vacia = ninguna. Desde el panel se puede
 *                 cambiar en cualquier momento; lo que se configure alli queda
 *                 guardado en el rover y manda sobre esto.
 */
#define ROVER_AP_SSID  "ROVER-6x6"
#define ROVER_AP_PASS  "cambia-esta-clave"

#define ROVER_STA_SSID ""
#define ROVER_STA_PASS ""
