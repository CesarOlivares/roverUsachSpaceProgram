#pragma once

#include <stdint.h>

/* =========================================================================
 * Mapa de GPIO del Rover 6x6 - ESP32-S3 (N16R8)
 *
 * Cada motor usa dos pines del L298N:
 *   IN1 -> portadora PWM (canal LEDC fijo, asignado en MotorDriver.cpp)
 *   IN2 -> sentido (GPIO de salida simple)
 * ========================================================================= */

namespace pins {

// --- Driver 3 (eje delantero: M1 izquierda, M2 derecha) ---
constexpr int kM1In1 = 40;  // M1 delantero izq  - PWM
constexpr int kM1In2 = 39;  // M1 delantero izq  - sentido
constexpr int kM2In1 = 38;  // M2 delantero der  - PWM
constexpr int kM2In2 = 16;  // M2 delantero der  - sentido (movido del 37: choca con la PSRAM octal)

// --- Driver 2 (eje central: M3 izquierda, M4 derecha) ---
constexpr int kM3In1 = 1;   // M3 central izq    - PWM
constexpr int kM3In2 = 2;   // M3 central izq    - sentido
constexpr int kM4In1 = 42;  // M4 central der    - PWM
constexpr int kM4In2 = 41;  // M4 central der    - sentido

// --- Driver 1 (eje trasero: M5 izquierda, M6 derecha) ---
// M5 se movio de 19/20 a 17/18: los GPIO 19 y 20 son las lineas D- y D+ del
// USB nativo del ESP32-S3 y ocuparlos inhabilitaba el USB-Serial-JTAG.
constexpr int kM5In1 = 17;  // M5 trasero izq    - PWM
constexpr int kM5In2 = 18;  // M5 trasero izq    - sentido
constexpr int kM6In1 = 21;  // M6 trasero der    - PWM
constexpr int kM6In2 = 47;  // M6 trasero der    - sentido

// --- Paso a paso (DRV8825) ---
constexpr int kStepperStep = 6;
constexpr int kStepperDir  = 7;
constexpr int kStepperEn   = 8;   // activo en bajo

// --- Bus I2C: expansor de servos PCA9685 ---
constexpr int     kI2cSda      = 4;
constexpr int     kI2cScl      = 5;
constexpr uint8_t kPca9685Addr = 0x40;

}  // namespace pins

/* --- Reservados: NO USAR ---
 * GPIO 0, 3, 45, 46  strapping del ESP32-S3
 * GPIO 19, 20        D- y D+ del USB nativo (USB-Serial-JTAG)
 * GPIO 43, 44        consola UART0 (enlace por cable con el panel)
 * GPIO 26..37        flash y PSRAM octal del modulo N16R8
 *
 * --- Libres para ampliaciones ---
 * GPIO 9             ADC1 canal 8 (previsto para un divisor de bateria, sin montar)
 * GPIO 10..14, 15    GPIO/ADC. El 15 estaba previsto para un receptor Flysky i-BUS.
 */
