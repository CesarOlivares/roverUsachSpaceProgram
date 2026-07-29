#ifndef PINOUT_H
#define PINOUT_H

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Strict GPIO Mapping for ESP32-S3 (N16R8) - 6x6 Rover Hardware
 * ========================================================================= */

// --- Motor Driver 1 (Front Axle: Motor 1 Left, Motor 2 Right) ---
#define GPIO_M1_PWM     4   // Motor 1 Front Left PWM
#define GPIO_M1_DIR_A   5   // Motor 1 Front Left DIR A
#define GPIO_M1_DIR_B   6   // Motor 1 Front Left DIR B

#define GPIO_M2_PWM     7   // Motor 2 Front Right PWM
#define GPIO_M2_DIR_A   15  // Motor 2 Front Right DIR A
#define GPIO_M2_DIR_B   16  // Motor 2 Front Right DIR B

// --- Motor Driver 2 (Middle Axle: Motor 3 Left, Motor 4 Right) ---
#define GPIO_M3_PWM     17  // Motor 3 Middle Left PWM
#define GPIO_M3_DIR_A   18  // Motor 3 Middle Left DIR A
#define GPIO_M3_DIR_B   8   // Motor 3 Middle Left DIR B

#define GPIO_M4_PWM     9   // Motor 4 Middle Right PWM
#define GPIO_M4_DIR_A   10  // Motor 4 Middle Right DIR A
#define GPIO_M4_DIR_B   11  // Motor 4 Middle Right DIR B

// --- Motor Driver 3 (Rear Axle: Motor 5 Left, Motor 6 Right) ---
#define GPIO_M5_PWM     12  // Motor 5 Rear Left PWM
#define GPIO_M5_DIR_A   13  // Motor 5 Rear Left DIR A
#define GPIO_M5_DIR_B   14  // Motor 5 Rear Left DIR B

#define GPIO_M6_PWM     1   // Motor 6 Rear Right PWM
#define GPIO_M6_DIR_A   2   // Motor 6 Rear Right DIR A
#define GPIO_M6_DIR_B   42  // Motor 6 Rear Right DIR B

// --- Stepper Motor Driver (DRV8825 Mechanism) ---
#define GPIO_STEPPER_STEP   39  // Step pulse output
#define GPIO_STEPPER_DIR    38  // Direction control
#define GPIO_STEPPER_EN     37  // Driver active low enable

// --- I2C Bus - Servo Driver (PCA9685) ---
#define GPIO_I2C_SDA        41  // I2C SDA
#define GPIO_I2C_SCL        40  // I2C SCL
#define PCA9685_I2C_ADDR    0x40

// --- Telemetry & ADC ---
#define GPIO_BATTERY_ADC    35  // ADC1 Channel 4 (Battery voltage sensing)
#define GPIO_RC_IBUS_RX     36  // UART1 RX (Flysky i-BUS receiver)

// --- Reserved / Prohibited GPIOs (DO NOT USE) ---
// GPIO 0, 3, 45, 46 (ESP32-S3 Strapping pins)
// GPIO 43, 44 (UART0 Console Default)

#ifdef __cplusplus
}
#endif

#endif // PINOUT_H
