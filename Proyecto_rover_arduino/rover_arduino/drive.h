#ifndef DRIVE_H
#define DRIVE_H

#include <Arduino.h>
#include "pinout.h"

/*
 * Canal unico de traccion sobre BTS7960.
 *
 * Avance:    duty en RPWM, LPWM a 0.
 * Retroceso: duty en LPWM, RPWM a 0.
 * Parada:    ambos a 0 (rueda libre).
 *
 * Al cambiar de sentido se pone a cero primero el lado que estaba activo,
 * para que nunca haya PWM en ambas mitades del puente a la vez.
 */

static const uint32_t DRIVE_DUTY_MAX = (1u << DRIVE_PWM_BITS) - 1;
static float g_drive_pct = 0.0f;

void driveInit() {
  ledcAttach(PIN_DRIVE_RPWM, DRIVE_PWM_FREQ, DRIVE_PWM_BITS);
  ledcAttach(PIN_DRIVE_LPWM, DRIVE_PWM_FREQ, DRIVE_PWM_BITS);
  ledcWrite(PIN_DRIVE_RPWM, 0);
  ledcWrite(PIN_DRIVE_LPWM, 0);
#if PIN_DRIVE_EN >= 0
  pinMode(PIN_DRIVE_EN, OUTPUT);
  digitalWrite(PIN_DRIVE_EN, HIGH);
#endif
}

void driveSetPercent(float pct) {
  if (pct > 100.0f)  pct = 100.0f;
  if (pct < -100.0f) pct = -100.0f;
  g_drive_pct = pct;

  uint32_t duty = (uint32_t)((fabsf(pct) / 100.0f) * DRIVE_DUTY_MAX + 0.5f);
  if (pct > 0.0f) {
    ledcWrite(PIN_DRIVE_LPWM, 0);
    ledcWrite(PIN_DRIVE_RPWM, duty);
  } else if (pct < 0.0f) {
    ledcWrite(PIN_DRIVE_RPWM, 0);
    ledcWrite(PIN_DRIVE_LPWM, duty);
  } else {
    ledcWrite(PIN_DRIVE_RPWM, 0);
    ledcWrite(PIN_DRIVE_LPWM, 0);
  }
}

void driveStop() {
  driveSetPercent(0.0f);
}

float driveGetPercent() {
  return g_drive_pct;
}

#endif // DRIVE_H
