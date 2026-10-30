#pragma once

#include <stdint.h>
#include "freertos/FreeRTOS.h"

/*
 * Expansor de servos PCA9685 por I2C (Wire) a 50 Hz.
 *
 * ESCRITURA DIFERIDA: setAngle() solo apunta la consigna en RAM. Una tarea
 * propia es la UNICA que habla por el bus I2C. Una transaccion I2C bloquea
 * (Wire espera hasta 50 ms si el bus sufre), y antes se hacia desde la tarea
 * que recibe la radio: mover los servos frenaba la recepcion de las tramas
 * siguientes y saltaba el failsafe. Es la misma medida que tenia la version
 * Arduino de la rama feature/arduino-4ws-single-drive, que funcionaba.
 *
 * La tarea ademas vigila el chip: si un pico de corriente de los servos lo
 * resetea, pierde el prescale de 50 Hz y los servos quedan mudos. Cada segundo
 * se comprueba el registro PRESCALE y, si no cuadra, se reconfigura y se
 * reponen los angulos.
 */
class ServoDriver {
public:
    static constexpr int kChannels = 16;   // canales fisicos del integrado

    /**
     * Levanta el bus I2C, configura el PCA9685 y lanza la tarea de escritura.
     * No falla si la placa no responde: avisa y la tarea reintenta cada segundo.
     * Solo devuelve false si no se pudo abrir el bus o crear la tarea.
     */
    bool begin();

    /**
     * Apunta la consigna de un servo. NO toca el bus: es seguro llamarla desde
     * cualquier tarea y vuelve en microsegundos.
     * @param angle 0..180 grados; se acota.
     * @return el angulo aceptado tras acotar, o NAN si el canal o el valor no valen.
     */
    float setAngle(int channel, float angle);

private:
    static void task(void *arg);
    void        loop();
    void        applyPending();
    void        healthCheck();
    bool        configureChip();
    bool        writeReg(uint8_t reg, uint8_t val);
    bool        readReg(uint8_t reg, uint8_t &val);

    portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    float        target_[kChannels];      // consigna pedida; < 0 = sin consigna
    int32_t      lastTicks_[kChannels];   // ultimo valor escrito; -1 = pendiente (solo la tarea)
    bool         chipOk_ = false;         // solo la tarea, salvo en begin()
    bool         started_ = false;
};
