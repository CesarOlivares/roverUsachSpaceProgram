/*
 * Rover 6x6 USACH Space Program - firmware Arduino (ESP32-S3 N16R8)
 *
 * Hardware: 6 motores DC en paralelo sobre un canal de BTS7960 (traccion) y
 * 4 servos de esquina en un PCA9685 (direccion 4WS). El panel se abre desde
 * el computador (index.html) y se conecta al WebSocket del SoftAP
 * "Rover_6x6_AP" (ws://192.168.4.1/ws) o por cable USB (Web Serial).
 *
 * Placa: "ESP32S3 Dev Module" | Flash 16MB | PSRAM: OPI PSRAM
 * USB CDC On Boot: Enabled (el enlace serie del panel va por el USB nativo)
 *
 * Librerias: ESP Async WebServer + Async TCP (ESP32Async),
 * Adafruit PWM Servo Driver, ArduinoJson 7.
 */

#include "pinout.h"
#include "drive.h"
#include "steering.h"
#include "rover_state.h"
#include "link.h"
#include "web_server.h"

void setup() {
  Serial.begin(115200);

  // Hardware en estado seguro antes de aceptar la primera orden.
  driveInit();
  steeringInit();
  stateMarkPacket();

  webInit();

  logLine("Rover 6x6 listo. AP %s -> ws://192.168.4.1/ws", AP_SSID);
  if (!steeringOk()) logLine("AVISO: PCA9685 no responde, direccion inactiva.");
}

void loop() {
  static uint32_t lastTelemetry = 0;
  static uint32_t lastCleanup   = 0;

  linkSerialPoll();
  linkFailsafeTick();
  driveRampTick();

  uint32_t now = millis();
  if (now - lastTelemetry >= 1000 / ROVER_TELEMETRY_HZ) {
    lastTelemetry = now;
    linkTelemetryTick();
  }
  if (now - lastCleanup >= 1000) {
    lastCleanup = now;
    webCleanupTick();
    steeringHealthTick();
  }
}
