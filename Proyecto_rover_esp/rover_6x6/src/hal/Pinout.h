#pragma once

#include <stdint.h>

/* =========================================================================
 * Mapa de GPIO del Rover 6x6 - ESP32-S3 (N16R8)
 *
 * Traccion: un unico puente H BTS7960 mueve todas las ruedas a la vez.
 * Direccion: servos en el PCA9685 (I2C).
 * ========================================================================= */

namespace pins {

// --- Traccion: BTS7960 (IBT-2) ---
// R_EN y L_EN van cableados fijos a 5 V: el puente siempre esta habilitado y
// el firmware solo gobierna las dos entradas PWM. Nunca se activan a la vez.
constexpr int kTractionRpwm = 5;   // RPWM - adelante
constexpr int kTractionLpwm = 4;   // LPWM - atras

// --- Bus I2C: expansor de servos PCA9685 ---
constexpr int     kI2cSda      = 8;
constexpr int     kI2cScl      = 9;
constexpr uint8_t kPca9685Addr = 0x40;

// --- Radio nRF24L01 (SPI2 / FSPI) ---
// 10..13 son los pines IO_MUX nativos de FSPI en el S3: el bus va sin pasar por
// la matriz GPIO. El modulo va a 3,3 V (NUNCA a 5 V) con un condensador de
// 10..100 uF entre VCC y GND junto al modulo.
constexpr int kNrfCsn  = 10;   // CSN / CS
constexpr int kNrfMosi = 11;
constexpr int kNrfSck  = 12;
constexpr int kNrfMiso = 13;
constexpr int kNrfCe   = 14;

// --- Paso a paso (DRV8825): SIN MONTAR ---
// El driver queda desactivado (cfg::kStepperMounted = false). Su antiguo EN
// (GPIO 8) es ahora el SDA del PCA9685: reasigna estos pines antes de montarlo.
constexpr int kStepperStep = 6;
constexpr int kStepperDir  = 7;
constexpr int kStepperEn   = 15;   // activo en bajo

}  // namespace pins

/* --- Reservados: NO USAR ---
 * GPIO 0, 3, 45, 46  strapping del ESP32-S3
 * GPIO 19, 20        D- y D+ del USB nativo (USB-Serial-JTAG)
 * GPIO 43, 44        consola UART0 (enlace por cable directo)
 * GPIO 26..37        flash y PSRAM octal del modulo N16R8
 * GPIO 48            LED RGB de la placa DevKitC-1
 */
