#pragma once

#include <stdint.h>

/*
 * Expansor de servos PCA9685 por I2C (Wire) a 50 Hz.
 *
 * No es thread-safe; lo llama unicamente CommandController.
 */
class ServoDriver {
public:
    static constexpr int kChannels = 16;   // canales fisicos del integrado

    /**
     * Levanta el bus I2C y deja el PCA9685 a 50 Hz.
     * No falla si la placa no responde: avisa y sigue, para que el rover pueda
     * rodar sin direccion. Solo devuelve false si no se pudo abrir el bus.
     */
    bool begin();

    /**
     * Posiciona un servo. Filtra escrituras repetidas para no saturar el bus.
     * @param angle 0..180 grados; se acota.
     * @return el angulo aplicado tras acotar, o NAN si el canal o el valor no valen.
     *         Si la escritura I2C falla tambien devuelve el angulo: es la consigna.
     */
    float setAngle(int channel, float angle);

private:
    bool writeReg(uint8_t reg, uint8_t val);
    bool readReg(uint8_t reg, uint8_t &val);

    bool    ready_ = false;
    // Ultimo tick escrito por canal; -1 = aun sin escribir. El lazo DRIVE llega
    // a 20 Hz y casi siempre pide el mismo angulo.
    int32_t lastTicks_[kChannels];
};
