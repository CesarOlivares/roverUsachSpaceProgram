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
#include "diag.h"
#include "drive.h"
#include "steering.h"
#include "rover_state.h"
#include "link.h"
#include "web_server.h"

void setup() {
  Serial.begin(115200);
  /*
   * Solo cuando Serial es el USB CDC nativo (USB CDC On Boot: Enabled). En esa
   * configuracion, si nadie lee el puerto, cada Serial.write() de telemetria
   * bloquea hasta 100 ms al llenarse el buffer: eso frena loop() y a logLine()
   * desde la tarea asincrona, y con el failsafe en 400 ms se nota. Con timeout
   * 0 se descarta lo que no quepa en vez de esperar.
   *
   * Con USB CDC On Boot: Disabled, Serial es la UART0 por hardware: drena sola
   * a 115200 baudios lea alguien o no, no existe ese bloqueo, y HardwareSerial
   * ni siquiera tiene el metodo. De ahi la guarda.
   */
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif

  // Hardware en estado seguro antes de aceptar la primera orden.
  driveInit();
  steeringInit();
  stateMarkPacket();

  webInit();

  diagEv(D_BOOT, 0);
  logLine("Rover 6x6 listo. AP %s -> ws://192.168.4.1/ws", AP_SSID);
  if (!steeringOk()) logLine("AVISO: PCA9685 no responde, direccion inactiva.");
}

void loop() {
  static uint32_t lastTelemetry = 0;
  static uint32_t lastCleanup   = 0;

  // Cronometra la vuelta entera: si loop() se atasca, el failsafe y la rampa
  // dejan de atenderse a tiempo y hay que saberlo.
  uint32_t loop_t0 = micros();

  linkSerialPoll();
  linkFailsafeTick();
  driveRampTick();
  // Unico punto que escribe los servos por I2C: el callback WS solo apunta la
  // consigna, para no bloquear la tarea de AsyncTCP con el bus.
  steeringApplyTick();

  uint32_t now = millis();
  if (now - lastTelemetry >= 1000 / ROVER_TELEMETRY_HZ) {
    lastTelemetry = now;
    linkTelemetryTick();
  }
  if (now - lastCleanup >= 1000) {
    lastCleanup = now;
    webCleanupTick();
    steeringHealthTick();
    diagHealthTick();
  }

  uint32_t loop_dt = micros() - loop_t0;
  diagMax(g_loop_max_us, loop_dt);
  if (loop_dt > DIAG_STALL_US) diagEv(D_STALL, (int32_t)(loop_dt / 1000));
}
