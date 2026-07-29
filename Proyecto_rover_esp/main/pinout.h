#ifndef PINOUT_H
#define PINOUT_H

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Strict GPIO Mapping for ESP32-S3 (N16R8) - 6x6 Rover Hardware
 * ========================================================================= */

// --- Motor Driver 3 (Front Axle: Motor 1 Left, Motor 2 Right) ---
#define GPIO_M1_IN1     40  // Motor 1 Front Left IN1
#define GPIO_M1_IN2     39  // Motor 1 Front Left IN2
#define GPIO_M2_IN1     38  // Motor 2 Front Right IN1
#define GPIO_M2_IN2     16  // Motor 2 Front Right IN2 (Moved from 37 due to Octal PSRAM conflict)

// --- Motor Driver 2 (Middle Axle: Motor 3 Left, Motor 4 Right) ---
#define GPIO_M3_IN1     1   // Motor 3 Middle Left IN1
#define GPIO_M3_IN2     2   // Motor 3 Middle Left IN2
#define GPIO_M4_IN1     42  // Motor 4 Middle Right IN1
#define GPIO_M4_IN2     41  // Motor 4 Middle Right IN2

// --- Motor Driver 1 (Rear Axle: Motor 5 Left, Motor 6 Right) ---
#define GPIO_M5_IN1     19  // Motor 5 Rear Left IN1
#define GPIO_M5_IN2     20  // Motor 5 Rear Left IN2
#define GPIO_M6_IN1     21  // Motor 6 Rear Right IN1
#define GPIO_M6_IN2     47  // Motor 6 Rear Right IN2

// --- Stepper Motor Driver (DRV8825 Mechanism) ---
#define GPIO_STEPPER_STEP   6   // Step pulse output (moved from 39)
#define GPIO_STEPPER_DIR    7   // Direction control (moved from 38)
#define GPIO_STEPPER_EN     8   // Driver active low enable (moved from 37)

// --- I2C Bus - Servo Driver (PCA9685) ---
#define GPIO_I2C_SDA        4   // I2C SDA (moved from 41)
#define GPIO_I2C_SCL        5   // I2C SCL (moved from 40)
#define PCA9685_I2C_ADDR    0x40

// --- Telemetry & ADC ---
#define GPIO_BATTERY_ADC    9   // ADC1 Channel 8 (Battery voltage sensing) (Moved from 35 due to Octal PSRAM conflict)
#define GPIO_RC_IBUS_RX     15  // UART1 RX (Flysky i-BUS receiver) (Moved from 36 due to Octal PSRAM conflict)

// --- Reserved / Prohibited GPIOs (DO NOT USE) ---
// GPIO 0, 3, 45, 46 (ESP32-S3 Strapping pins)
// GPIO 43, 44 (UART0 Console Default)

#ifdef __cplusplus
}
#endif

#endif // PINOUT_H
