#ifndef STEERING_H
#define STEERING_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include "pinout.h"

/*
 * Direccion 4WS con 4 servos por PCA9685 (canales 0..3).
 *
 * Si el PCA9685 no responde al arrancar se sigue adelante con un aviso: el
 * rover debe poder avanzar recto aunque la direccion este desconectada.
 *
 * Se cachea el ultimo angulo enviado por canal y se filtran las escrituras
 * repetidas, igual que en el firmware ESP-IDF: con DRIVE llegando a 20 Hz,
 * marchar recto no debe generar trafico I2C.
 */

static Adafruit_PWMServoDriver g_pca(PCA9685_I2C_ADDR);
static bool  g_pca_ok = false;
static float g_servo_deg[STEER_SERVO_COUNT] = { -1.0f, -1.0f, -1.0f, -1.0f };

// Escritura directa de un canal en grados [0..180], sin trim ni inversion.
void steeringSetChannelAngle(uint8_t ch, float deg) {
  if (ch >= STEER_SERVO_COUNT) return;
  if (deg < 0.0f)   deg = 0.0f;
  if (deg > 180.0f) deg = 180.0f;
  if (deg == g_servo_deg[ch]) return;
  g_servo_deg[ch] = deg;
  if (!g_pca_ok) return;
  uint16_t us = (uint16_t)(SERVO_MIN_US + (deg / 180.0f) * (SERVO_MAX_US - SERVO_MIN_US));
  g_pca.writeMicroseconds(ch, us);
}

/*
 * Direccion coordinada: angular -100..100 -> delta de hasta +-45 grados.
 * Delanteros (0,1) giran 90+delta y traseros (2,3) 90-delta, para cerrar el
 * radio de giro. El trim e inversion por servo se aplican al final.
 */
void steeringSetAngular(float angular) {
  if (angular > 100.0f)  angular = 100.0f;
  if (angular < -100.0f) angular = -100.0f;
  float delta = (angular / 100.0f) * STEER_THROW_DEG;

  for (uint8_t ch = 0; ch < STEER_SERVO_COUNT; ch++) {
    float d = (ch < 2) ? delta : -delta;               // 0,1 delanteros; 2,3 traseros
    float deg = STEER_CENTER_DEG + SERVO_INVERT[ch] * d + SERVO_TRIM_DEG[ch];
    if (deg < STEER_CENTER_DEG - STEER_THROW_DEG) deg = STEER_CENTER_DEG - STEER_THROW_DEG;
    if (deg > STEER_CENTER_DEG + STEER_THROW_DEG) deg = STEER_CENTER_DEG + STEER_THROW_DEG;
    steeringSetChannelAngle(ch, deg);
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

void steeringInit() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);
  g_pca_ok = g_pca.begin();
  if (g_pca_ok) {
    g_pca.setPWMFreq(SERVO_FREQ_HZ);
    steeringCenterAll();
  }
}

#endif // STEERING_H
