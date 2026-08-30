#ifndef PINOUT_H
#define PINOUT_H

/* =========================================================================
 * Mapa de GPIO del Rover 6x6 - ESP32-S3 (N16R8) - version Arduino
 *
 * Traccion: los 6 motores DC van EN PARALELO a un solo canal de un puente H
 * BTS7960 / IBT-2. Solo hay una consigna de velocidad para todo el rover.
 *   RPWM -> PWM de avance
 *   LPWM -> PWM de retroceso
 *   R_EN + L_EN -> cableados juntos a un GPIO (HIGH = etapa habilitada)
 *
 * Direccion: 4 servos, uno por esquina, en el PCA9685 (canales 0..3).
 * El giro es 4WS coordinado: delanteros y traseros giran en sentido opuesto.
 * ========================================================================= */

// --- Puente H BTS7960 (canal unico de traccion M1..M6) ---
#define PIN_DRIVE_RPWM      4   // PWM avance
#define PIN_DRIVE_LPWM      5   // PWM retroceso
#define PIN_DRIVE_EN        6   // R_EN+L_EN juntos. Poner -1 si van fijos a 5V.

// --- Bus I2C: expansor de servos PCA9685 ---
// GPIO 8/9 son los pines I2C por defecto del core Arduino en el S3.
#define PIN_I2C_SDA         8
#define PIN_I2C_SCL         9
#define PCA9685_I2C_ADDR    0x40

/* --- Reservados: NO USAR ---
 * GPIO 0, 3, 45, 46  strapping del ESP32-S3
 * GPIO 19, 20        D- y D+ del USB nativo (USB-Serial-JTAG)
 * GPIO 43, 44        UART0
 * GPIO 26..37        flash y PSRAM octal del modulo N16R8
 *
 * --- Libres para ampliaciones ---
 * GPIO 1, 2, 7, 10..18, 21, 38..42, 47, 48
 */

// --- PWM de traccion ---
// 20 kHz queda fuera del rango audible; 10 bits caben de sobra en el LEDC.
#define DRIVE_PWM_FREQ      20000
#define DRIVE_PWM_BITS      10

// Rampa de aceleracion: la consigna real se acerca a la pedida a este ritmo.
// 6 motores en paralelo arrancando de golpe hunden la fuente; con 150 %/s el
// 0->100 tarda ~0.7 s. La parada de emergencia y el failsafe cortan en seco.
#define DRIVE_RAMP_PCT_PER_S    150.0f

// --- Servos de direccion ---
#define SERVO_FREQ_HZ       50
#define SERVO_MIN_US        500     // 0 grados
#define SERVO_MAX_US        2500    // 180 grados
#define STEER_CENTER_DEG    90.0f
#define STEER_THROW_DEG     45.0f   // 45 izq <- 90 centro -> 135 der

// Canales del PCA9685: 0=FL (del. izq), 1=FR (del. der), 2=RL (tras. izq), 3=RR (tras. der)
#define STEER_SERVO_COUNT   4

// Ajuste mecanico por servo: correccion fina del centro y sentido de giro
// segun como quedo montado cada servo en la esquina.
static const float  SERVO_TRIM_DEG[STEER_SERVO_COUNT] = { 0.0f, 0.0f, 0.0f, 0.0f };
static const int8_t SERVO_INVERT[STEER_SERVO_COUNT]   = { 1, 1, 1, 1 };  // -1 invierte

// --- Enlace ---
#define AP_SSID                     "Rover_6x6_AP"
#define AP_PASS                     "Rover6x6Secure"
#define ROVER_TELEMETRY_HZ          20    // ritmo al que se EVALUA si hay que emitir
/*
 * Failsafe: corte automatico de potencia cuando el enlace calla.
 *
 *   1 = desarma y corta la traccion tras ROVER_FAILSAFE_TIMEOUT_MS sin tramas.
 *   0 = NO corta nada.
 *
 * ATENCION con 0: si el enlace se cae mientras el rover rueda, el rover SIGUE
 * RODANDO a la ultima consigna recibida hasta que el enlace vuelva o se corte
 * la alimentacion. No queda ninguna parada automatica; STOP y DISARM solo
 * llegan si hay enlace. Tener a mano el interruptor de la fuente.
 *
 * Con 0 el silencio se sigue DETECTANDO y registrando (lineas ENLACE MUDO /
 * ENLACE RECUPERADO mas volcado DIAG): se pierde el corte, no el diagnostico.
 */
#define ROVER_FAILSAFE_ENABLED      0

// Umbral de silencio. Con el failsafe activo es el corte; con 0, el aviso.
#define ROVER_FAILSAFE_TIMEOUT_MS   400
// La telemetria solo se emite cuando cambia el estado; si no cambia nada se
// manda un latido cada tanto para que el panel no marque el enlace caido
// (su umbral de "sin datos" es 800 ms).
#define ROVER_TELEMETRY_KEEPALIVE_MS 500

#endif // PINOUT_H
