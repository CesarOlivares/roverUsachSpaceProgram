#ifndef PINOUT_H
#define PINOUT_H

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Mapa de GPIO del Rover 6x6 - ESP32-S3 (N16R8)
 *
 * Cada motor usa dos pines del L298N:
 *   IN1 -> portadora PWM (canal LEDC fijo, asignado en mcpwm_motors.c)
 *   IN2 -> sentido (GPIO de salida simple)
 * ========================================================================= */

// --- Driver 3 (eje delantero: M1 izquierda, M2 derecha) ---
#define GPIO_M1_IN1     40  // M1 delantero izq  - PWM
#define GPIO_M1_IN2     39  // M1 delantero izq  - sentido
#define GPIO_M2_IN1     38  // M2 delantero der  - PWM
#define GPIO_M2_IN2     16  // M2 delantero der  - sentido (movido del 37: choca con la PSRAM octal)

// --- Driver 2 (eje central: M3 izquierda, M4 derecha) ---
#define GPIO_M3_IN1     1   // M3 central izq    - PWM
#define GPIO_M3_IN2     2   // M3 central izq    - sentido
#define GPIO_M4_IN1     42  // M4 central der    - PWM
#define GPIO_M4_IN2     41  // M4 central der    - sentido

// --- Driver 1 (eje trasero: M5 izquierda, M6 derecha) ---
// M5 se movio de 19/20 a 17/18: los GPIO 19 y 20 son las lineas D- y D+ del
// USB nativo del ESP32-S3 y ocuparlos inhabilitaba el USB-Serial-JTAG.
#define GPIO_M5_IN1     17  // M5 trasero izq    - PWM
#define GPIO_M5_IN2     18  // M5 trasero izq    - sentido
#define GPIO_M6_IN1     21  // M6 trasero der    - PWM
#define GPIO_M6_IN2     47  // M6 trasero der    - sentido

// --- Paso a paso (DRV8825) ---
#define GPIO_STEPPER_STEP   6
#define GPIO_STEPPER_DIR    7
#define GPIO_STEPPER_EN     8   // activo en bajo

// --- Bus I2C: expansor de servos PCA9685 ---
#define GPIO_I2C_SDA        4
#define GPIO_I2C_SCL        5
#define PCA9685_I2C_ADDR    0x40

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

#ifdef __cplusplus
}
#endif

#endif // PINOUT_H
