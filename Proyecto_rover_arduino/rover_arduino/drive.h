#ifndef DRIVE_H
#define DRIVE_H

#include <Arduino.h>
#include "pinout.h"

/*
 * Canal unico de traccion sobre BTS7960, con rampa de aceleracion.
 *
 * Avance:    duty en RPWM, LPWM a 0.
 * Retroceso: duty en LPWM, RPWM a 0.
 * Parada:    ambos a 0 (rueda libre).
 *
 * Al cambiar de sentido se pone a cero primero el lado que estaba activo,
 * para que nunca haya PWM en ambas mitades del puente a la vez.
 *
 * La consigna pedida (target) no se aplica de golpe: driveRampTick() la
 * persigue a DRIVE_RAMP_PCT_PER_S para no hundir la fuente con el arranque
 * de los 6 motores. driveStop() es la excepcion: corta en seco (seguridad).
 */

static const uint32_t DRIVE_DUTY_MAX = (1u << DRIVE_PWM_BITS) - 1;
static float g_drive_pct = 0.0f;      // lo que esta aplicado ahora mismo
static float g_drive_target = 0.0f;   // lo que pidio el operador

static void driveApply(float pct) {
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

void driveInit() {
  ledcAttach(PIN_DRIVE_RPWM, DRIVE_PWM_FREQ, DRIVE_PWM_BITS);
  ledcAttach(PIN_DRIVE_LPWM, DRIVE_PWM_FREQ, DRIVE_PWM_BITS);
  driveApply(0.0f);
#if PIN_DRIVE_EN >= 0
  pinMode(PIN_DRIVE_EN, OUTPUT);
  digitalWrite(PIN_DRIVE_EN, HIGH);
#endif
}

// Fija la consigna objetivo; la rampa la alcanza gradualmente.
void driveSetPercent(float pct) {
  if (pct > 100.0f)  pct = 100.0f;
  if (pct < -100.0f) pct = -100.0f;
  g_drive_target = pct;
}

// Corte inmediato, sin rampa: parada de emergencia, DISARM y failsafe.
void driveStop() {
  g_drive_target = 0.0f;
  driveApply(0.0f);
}

// Llamar desde loop(): acerca la salida real a la consigna al ritmo de rampa.
void driveRampTick() {
  static uint32_t last_ms = 0;
  uint32_t now = millis();
  if (last_ms == 0) { last_ms = now; return; }
  float dt = (now - last_ms) / 1000.0f;
  last_ms = now;

  if (g_drive_pct == g_drive_target) return;
  float step = DRIVE_RAMP_PCT_PER_S * dt;
  float next = g_drive_pct;
  if (next < g_drive_target) {
    next += step;
    if (next > g_drive_target) next = g_drive_target;
  } else {
    next -= step;
    if (next < g_drive_target) next = g_drive_target;
  }
  driveApply(next);
}

float driveGetPercent() {
  return g_drive_pct;
}

// Hay traccion en marcha o pedida: lo vigila el failsafe.
bool driveIsActive() {
  return (g_drive_pct != 0.0f) || (g_drive_target != 0.0f);
}

#endif // DRIVE_H
