#include "ServoDriver.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "freertos/task.h"
#include "Pinout.h"
#include "../util/Log.h"

#define I2C_FREQ_HZ       400000      // 400 kHz: el bus debe aguantar 5 servos a 20 Hz
#define I2C_TIMEOUT_MS    50

#define APPLY_PERIOD_MS   20          // volcado de consignas: 50 Hz, el ritmo del PWM de los servos
#define HEALTH_PERIOD_MS  1000        // vigilancia del chip
#define SLOW_WRITE_US     20000       // una escritura por encima de esto = bus sufriendo

#define REG_MODE1         0x00
#define REG_PRESCALE      0xFE
#define REG_LED0_ON_L     0x06

#define MODE1_ALLCALL     0x01
#define MODE1_SLEEP       0x10
#define MODE1_AI          0x20
#define MODE1_RESTART     0x80

// Prescaler para 50 Hz con el oscilador interno de 25 MHz:
// round(25e6 / (4096 * 50)) - 1 = 121
#define PRESCALE_50HZ     121

#define SERVO_MIN_US      500.0f
#define SERVO_MAX_US      2500.0f
#define PWM_PERIOD_US     20000.0f    // 50 Hz
#define PWM_TICKS         4096.0f

bool ServoDriver::writeReg(uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(pins::kPca9685Addr);
    Wire.write(reg);
    Wire.write(val);
    return Wire.endTransmission() == 0;
}

bool ServoDriver::readReg(uint8_t reg, uint8_t &val)
{
    Wire.beginTransmission(pins::kPca9685Addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(pins::kPca9685Addr, (uint8_t)1) != 1) return false;
    val = (uint8_t)Wire.read();
    return true;
}

/*
 * Secuencia de arranque segun hoja de datos (7.3.1.1): el prescaler solo
 * admite escritura con SLEEP activo, y tras despertar hay que esperar
 * >=500 us a que el oscilador arranque antes de emitir RESTART.
 * Devuelve true si el chip responde y quedo a 50 Hz.
 */
bool ServoDriver::configureChip()
{
    uint8_t mode1 = MODE1_ALLCALL;
    if (!readReg(REG_MODE1, mode1)) return false;

    writeReg(REG_MODE1, (uint8_t)((mode1 & ~MODE1_RESTART) | MODE1_SLEEP));
    writeReg(REG_PRESCALE, PRESCALE_50HZ);
    writeReg(REG_MODE1, (uint8_t)((mode1 & ~MODE1_SLEEP) | MODE1_AI));
    delay(5);
    writeReg(REG_MODE1, (uint8_t)((mode1 & ~MODE1_SLEEP) | MODE1_AI | MODE1_RESTART));

    uint8_t prescale = 0;
    return readReg(REG_PRESCALE, prescale) && prescale == PRESCALE_50HZ;
}

bool ServoDriver::begin()
{
    if (started_) return true;

    Log::info("PCA9685", "Inicializando expansor de servos PCA9685 (SDA %d, SCL %d)...",
              pins::kI2cSda, pins::kI2cScl);

    for (int i = 0; i < kChannels; i++) {
        target_[i]    = -1.0f;
        lastTicks_[i] = -1;
    }

    if (!Wire.begin(pins::kI2cSda, pins::kI2cScl, I2C_FREQ_HZ)) {
        Log::warn("PCA9685", "No se pudo abrir el bus I2C. El rover sigue sin direccion.");
        return false;
    }
    Wire.setTimeOut(I2C_TIMEOUT_MS);

    chipOk_ = configureChip();
    if (chipOk_) {
        Log::info("PCA9685", "PCA9685 listo a 50 Hz (prescale %d).", PRESCALE_50HZ);
    } else {
        // No es fatal: el rover debe poder rodar aunque falte la placa de servos.
        Log::warn("PCA9685", "El PCA9685 no responde en 0x%02X. Revisa alimentacion y cableado.",
                  pins::kPca9685Addr);
    }

    // Nucleo 1: lejos de la radio y del enlace por cable, que van en el 0.
    if (xTaskCreatePinnedToCore(task, "servo_io", 3072, this, 3, nullptr, 1) != pdPASS) {
        return false;
    }
    started_ = true;
    return true;
}

float ServoDriver::setAngle(int channel, float angle)
{
    if (channel < 0 || channel >= kChannels) return NAN;
    if (isnan(angle)) return NAN;

    if (angle < 0.0f)   angle = 0.0f;
    if (angle > 180.0f) angle = 180.0f;

    portENTER_CRITICAL(&lock_);
    target_[channel] = angle;
    portEXIT_CRITICAL(&lock_);
    return angle;
}

void ServoDriver::task(void *arg) { static_cast<ServoDriver *>(arg)->loop(); }

void ServoDriver::loop()
{
    TickType_t last_wake   = xTaskGetTickCount();
    uint32_t   last_health = millis();

    while (true) {
        applyPending();

        uint32_t now = millis();
        if (now - last_health >= HEALTH_PERIOD_MS) {
            last_health = now;
            healthCheck();
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(APPLY_PERIOD_MS));
    }
}

// Vuelca al PCA9685 las consignas que hayan cambiado. Es el unico sitio que
// escribe los servos por I2C. Marchar recto no genera trafico: se filtran
// los valores repetidos.
void ServoDriver::applyPending()
{
    if (!chipOk_) return;

    float targets[kChannels];
    portENTER_CRITICAL(&lock_);
    for (int i = 0; i < kChannels; i++) targets[i] = target_[i];
    portEXIT_CRITICAL(&lock_);

    for (int ch = 0; ch < kChannels; ch++) {
        if (targets[ch] < 0.0f) continue;

        float   pulse_us = SERVO_MIN_US + (targets[ch] / 180.0f) * (SERVO_MAX_US - SERVO_MIN_US);
        int32_t ticks    = (int32_t)((pulse_us * PWM_TICKS) / PWM_PERIOD_US + 0.5f);
        if (ticks < 0)    ticks = 0;
        if (ticks > 4095) ticks = 4095;
        if (lastTicks_[ch] == ticks) continue;

        uint32_t t0 = micros();

        // Auto-increment activo: ON_L, ON_H, OFF_L, OFF_H en una sola trama.
        Wire.beginTransmission(pins::kPca9685Addr);
        Wire.write((uint8_t)(REG_LED0_ON_L + 4 * ch));
        Wire.write((uint8_t)0x00);
        Wire.write((uint8_t)0x00);
        Wire.write((uint8_t)(ticks & 0xFF));
        Wire.write((uint8_t)((ticks >> 8) & 0x0F));
        bool ok = Wire.endTransmission() == 0;

        uint32_t dt = micros() - t0;
        if (dt > SLOW_WRITE_US) {
            // Bus I2C sufriendo: pull-ups, ruido de los servos o alimentacion.
            Log::warn("PCA9685", "Escritura I2C lenta: %u ms en el canal %d.",
                      (unsigned)(dt / 1000), ch);
        }
        if (ok) lastTicks_[ch] = ticks;
    }
}

/*
 * Watchdog del PCA9685. Si la alimentacion se hunde por un pico de corriente,
 * el chip se resetea solo y pierde el prescale de 50 Hz: los servos dejan de
 * responder aunque "nada haya cambiado". Se detecta leyendo PRESCALE; si no
 * coincide, se reconfigura y se reponen los angulos que habia.
 */
void ServoDriver::healthCheck()
{
    uint8_t prescale = 0;
    if (chipOk_ && readReg(REG_PRESCALE, prescale) && prescale == PRESCALE_50HZ) return;

    bool was_ok = chipOk_;
    chipOk_ = configureChip();

    if (chipOk_) {
        for (int i = 0; i < kChannels; i++) lastTicks_[i] = -1;   // reponer todo
        applyPending();
        Log::warn("PCA9685", "%s: reconfigurado a 50 Hz.",
                  was_ok ? "El PCA9685 se reseteo (caida de alimentacion?)" : "PCA9685 detectado");
    } else if (was_ok) {
        Log::warn("PCA9685", "El PCA9685 dejo de responder en el bus I2C.");
    }
}
