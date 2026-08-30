#ifndef STEERING_H
#define STEERING_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include "pinout.h"
#include "diag.h"

/*
 * Direccion 4WS con 4 servos por PCA9685 (canales 0..3).
 *
 * Si el PCA9685 no responde al arrancar se sigue adelante con un aviso: el
 * rover debe poder avanzar recto aunque la direccion este desconectada.
 *
 * IMPORTANTE - por que la escritura I2C es diferida:
 *   steeringSetAngular() y steeringSetChannelAngle() se llaman desde el
 *   callback del WebSocket, que corre en la tarea de AsyncTCP. Una transaccion
 *   I2C es BLOQUEANTE (Wire tiene un timeout de 50 ms por defecto), y bloquear
 *   ahi frena la recepcion de las tramas siguientes: el failsafe de 400 ms
 *   saltaba justo al mover los servos. Ahora esas funciones solo apuntan la
 *   consigna en RAM y es steeringApplyTick(), desde loop(), quien habla por el
 *   bus. El callback asincrono no toca I2C nunca.
 *
 * Se cachea el ultimo angulo realmente enviado por canal y se filtran las
 * escrituras repetidas: con DRIVE llegando a 20 Hz, marchar recto no debe
 * generar trafico I2C.
 */

// Definido en link.h; se declara aqui porque steering.h se incluye antes.
static void logLine(const char *fmt, ...);

static Adafruit_PWMServoDriver g_pca(PCA9685_I2C_ADDR);
static bool    g_pca_ok = false;
static uint8_t g_pca_prescale = 0;    // valor esperado del registro PRESCALE

// Consigna pedida por el operador (la que se publica en telemetria).
static float g_servo_deg[STEER_SERVO_COUNT]  = { -1.0f, -1.0f, -1.0f, -1.0f };
// Ultimo angulo que llego de verdad al PCA9685. -1 = pendiente de escribir.
static float g_servo_sent[STEER_SERVO_COUNT] = { -1.0f, -1.0f, -1.0f, -1.0f };

// Apunta la consigna de un canal en grados [0..180]. NO toca el bus I2C.
void steeringSetChannelAngle(uint8_t ch, float deg) {
  if (ch >= STEER_SERVO_COUNT) return;
  if (isnan(deg)) return;
  if (deg < 0.0f)   deg = 0.0f;
  if (deg > 180.0f) deg = 180.0f;
  g_servo_deg[ch] = deg;
}

/*
 * Direccion coordinada: angular -100..100 -> delta de hasta +-45 grados.
 * Delanteros (0,1) giran 90+delta y traseros (2,3) 90-delta, para cerrar el
 * radio de giro.
 *
 * El recorrido se acota ANTES de sumar el trim: si se acotara despues, un trim
 * distinto de cero se perderia justo a tope de giro, que es donde la geometria
 * mas lo necesita.
 */
void steeringSetAngular(float angular) {
  if (isnan(angular)) return;
  if (angular > 100.0f)  angular = 100.0f;
  if (angular < -100.0f) angular = -100.0f;
  float delta = (angular / 100.0f) * STEER_THROW_DEG;

  for (uint8_t ch = 0; ch < STEER_SERVO_COUNT; ch++) {
    float d = (ch < 2) ? delta : -delta;               // 0,1 delanteros; 2,3 traseros
    d *= SERVO_INVERT[ch];
    if (d >  STEER_THROW_DEG) d =  STEER_THROW_DEG;
    if (d < -STEER_THROW_DEG) d = -STEER_THROW_DEG;
    steeringSetChannelAngle(ch, STEER_CENTER_DEG + d + SERVO_TRIM_DEG[ch]);
  }
}

void steeringCenterAll() {
  steeringSetAngular(0.0f);
}

float steeringGetAngle(uint8_t ch) {
  if (ch >= STEER_SERVO_COUNT) return STEER_CENTER_DEG;
  return (g_servo_deg[ch] < 0.0f) ? STEER_CENTER_DEG : g_servo_deg[ch];
}

bool steeringOk() {
  return g_pca_ok;
}

/*
 * Vuelca al PCA9685 las consignas que hayan cambiado. Llamar desde loop() en
 * cada vuelta: es el UNICO sitio que habla por I2C con los servos.
 */
void steeringApplyTick() {
  if (!g_pca_ok) return;
  for (uint8_t ch = 0; ch < STEER_SERVO_COUNT; ch++) {
    float deg = g_servo_deg[ch];
    if (deg < 0.0f || deg == g_servo_sent[ch]) continue;
    uint16_t us = (uint16_t)(SERVO_MIN_US + (deg / 180.0f) * (SERVO_MAX_US - SERVO_MIN_US));
    uint32_t t0 = micros();
    g_pca.writeMicroseconds(ch, us);
    uint32_t dt = micros() - t0;
    diagMax(g_i2c_max_us, dt);
    // Una escritura lenta aqui significa bus I2C sufriendo (pull-ups, ruido de
    // los servos, alimentacion): es candidato a frenar loop().
    if (dt > DIAG_I2C_US) diagEv(D_I2CSLOW, (int32_t)(dt / 1000));
    g_servo_sent[ch] = deg;
  }
}

void steeringInit() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);
  g_pca_ok = g_pca.begin();
  if (g_pca_ok) {
    g_pca.setPWMFreq(SERVO_FREQ_HZ);
    g_pca_prescale = g_pca.readPrescale();
    steeringCenterAll();
    steeringApplyTick();     // en setup() si se puede escribir de inmediato
  }
}

/*
 * Watchdog del PCA9685, llamar ~1 vez por segundo desde loop().
 *
 * Si la alimentacion se hunde por un pico de corriente, el PCA9685 se resetea
 * solo y pierde el prescale de 50 Hz: los servos dejan de responder aunque
 * "nada haya cambiado". Se detecta leyendo el registro PRESCALE; si no
 * coincide con lo configurado, se reinicializa el chip y se reponen los
 * angulos que habia.
 */
void steeringHealthTick() {
  if (g_pca_ok && g_pca.readPrescale() == g_pca_prescale) return;

  bool estaba_ok = g_pca_ok;
  g_pca_ok = g_pca.begin();
  if (g_pca_ok) {
    g_pca.setPWMFreq(SERVO_FREQ_HZ);
    g_pca_prescale = g_pca.readPrescale();
    // Invalida lo "ya enviado": steeringApplyTick() repondra las consignas.
    for (uint8_t ch = 0; ch < STEER_SERVO_COUNT; ch++) g_servo_sent[ch] = -1.0f;
    steeringApplyTick();
    diagEv(D_PCARST, 0);
    logLine("PCA9685 se reseteo (caida de alimentacion?): reconfigurado a 50 Hz.");
  } else if (estaba_ok) {
    logLine("AVISO: PCA9685 dejo de responder en el bus I2C.");
  }
}

#endif // STEERING_H
