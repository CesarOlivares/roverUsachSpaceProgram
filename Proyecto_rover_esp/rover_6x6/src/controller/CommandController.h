#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "../common/Command.h"
#include "../model/RoverModel.h"
#include "../hal/MotorDriver.h"
#include "../hal/ServoDriver.h"
#include "../hal/StepperDriver.h"

/*
 * Controlador de mando: aplica las reglas (arbitraje, armado, limites) a cada
 * orden, mueve el hardware y deja el modelo al dia.
 *
 * Lo llaman a la vez la tarea RX del cable, la de la radio y el failsafe,
 * repartidos entre los dos nucleos. Un unico mutex serializa todo: asi el
 * arbitraje y la accion que autoriza son atomicos, y el modelo nunca refleja
 * una consigna distinta de la que tiene el hardware.
 */
class CommandController {
public:
    CommandController(RoverModel &model, MotorDriver &motors,
                      ServoDriver &servos, StepperDriver &stepper);

    bool begin();

    /** Ejecuta una orden ya decodificada. */
    void handle(const Command &cmd);

    /** Cuenta una trama que no se pudo decodificar. */
    void reportInvalidFrame();

    /** Lleva todos los servos a 90 grados. Para el arranque. */
    void centerServos();

    /** Snapshot coherente del estado para la telemetria. */
    RoverSnapshot snapshot();

    /* --- Para el failsafe --- */

    /**
     * true si hay algo que proteger (rover armado, hardware en movimiento o un
     * operador con el mando) y su operador lleva mas de @p timeoutUs callado.
     */
    bool controlLinkLost(int64_t timeoutUs);

    /** Desarma, para todo, libera el mando y reinicia el temporizador del enlace. */
    void failsafeStop();

    /** Arranca el temporizador del enlace desde ahora. */
    void resetLinkTimer();

private:
    void safeStopLocked();
    void driveLocked(float linear, float angular);
    void setServoLocked(int ch, float angle);

    RoverModel       &model_;
    MotorDriver      &motors_;
    ServoDriver      &servos_;
    StepperDriver    &stepper_;
    SemaphoreHandle_t mtx_ = nullptr;
};
