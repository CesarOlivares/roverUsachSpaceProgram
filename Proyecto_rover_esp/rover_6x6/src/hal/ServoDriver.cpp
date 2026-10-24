#include "ServoDriver.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include "Pinout.h"
#include "../util/Log.h"

#define I2C_FREQ_HZ     400000      // 400 kHz: el bus debe aguantar 5 servos a 20 Hz
#define I2C_TIMEOUT_MS  50

#define REG_MODE1       0x00
#define REG_PRESCALE    0xFE
#define REG_LED0_ON_L   0x06

#define MODE1_ALLCALL   0x01
#define MODE1_SLEEP     0x10
#define MODE1_AI        0x20
#define MODE1_RESTART   0x80

// Prescaler para 50 Hz con el oscilador interno de 25 MHz:
// round(25e6 / (4096 * 50)) - 1 = 121
#define PRESCALE_50HZ   121

#define SERVO_MIN_US    500.0f
#define SERVO_MAX_US    2500.0f
#define PWM_PERIOD_US   20000.0f    // 50 Hz
#define PWM_TICKS       4096.0f

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

bool ServoDriver::begin()
{
    if (ready_) return true;

    Log::info("PCA9685", "Inicializando expansor de servos PCA9685 (SDA %d, SCL %d)...",
              pins::kI2cSda, pins::kI2cScl);

    for (int i = 0; i < kChannels; i++) lastTicks_[i] = -1;

    if (!Wire.begin(pins::kI2cSda, pins::kI2cScl, I2C_FREQ_HZ)) {
        Log::warn("PCA9685", "No se pudo abrir el bus I2C. El rover sigue sin direccion.");
        return false;
    }
    Wire.setTimeOut(I2C_TIMEOUT_MS);

    Wire.beginTransmission(pins::kPca9685Addr);
    if (Wire.endTransmission() != 0) {
        // No es fatal: el rover debe poder rodar aunque falte la placa de servos.
        Log::warn("PCA9685", "El PCA9685 no responde en 0x%02X. Revisa alimentacion y cableado.",
                  pins::kPca9685Addr);
    }

    /*
     * Secuencia de arranque segun hoja de datos (7.3.1.1): el prescaler solo
     * admite escritura con SLEEP activo, y tras despertar hay que esperar
     * >=500 us a que el oscilador arranque antes de emitir RESTART.
     */
    uint8_t mode1 = MODE1_ALLCALL;
    readReg(REG_MODE1, mode1);

    writeReg(REG_MODE1, (uint8_t)((mode1 & ~MODE1_RESTART) | MODE1_SLEEP));
    writeReg(REG_PRESCALE, PRESCALE_50HZ);
    writeReg(REG_MODE1, (uint8_t)((mode1 & ~MODE1_SLEEP) | MODE1_AI));
    delay(5);
    writeReg(REG_MODE1, (uint8_t)((mode1 & ~MODE1_SLEEP) | MODE1_AI | MODE1_RESTART));

    ready_ = true;
    Log::info("PCA9685", "PCA9685 listo a 50 Hz (prescale %d).", PRESCALE_50HZ);
    return true;
}

float ServoDriver::setAngle(int channel, float angle)
{
    if (channel < 0 || channel >= kChannels) return NAN;
    if (isnan(angle)) return NAN;

    if (angle < 0.0f)   angle = 0.0f;
    if (angle > 180.0f) angle = 180.0f;
    if (!ready_) return angle;

    float   pulse_us = SERVO_MIN_US + (angle / 180.0f) * (SERVO_MAX_US - SERVO_MIN_US);
    int32_t ticks    = (int32_t)((pulse_us * PWM_TICKS) / PWM_PERIOD_US + 0.5f);
    if (ticks < 0)    ticks = 0;
    if (ticks > 4095) ticks = 4095;

    if (lastTicks_[channel] != ticks) {
        // Auto-increment activo: ON_L, ON_H, OFF_L, OFF_H en una sola trama.
        Wire.beginTransmission(pins::kPca9685Addr);
        Wire.write((uint8_t)(REG_LED0_ON_L + 4 * channel));
        Wire.write((uint8_t)0x00);
        Wire.write((uint8_t)0x00);
        Wire.write((uint8_t)(ticks & 0xFF));
        Wire.write((uint8_t)((ticks >> 8) & 0x0F));
        if (Wire.endTransmission() == 0) {
            lastTicks_[channel] = ticks;
        }
    }
    return angle;
}
