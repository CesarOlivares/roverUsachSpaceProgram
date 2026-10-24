/*
 * Rover 6x6 - ESP32-S3 (N16R8)
 *
 * 6 motores DC sobre 3 puentes L298N, 5 servos por PCA9685 y un paso a paso
 * DRV8825. El mando y la telemetria van por cable (Serial) y por el addon web
 * (SoftAP + HTTP + WebSocket), que pueden convivir.
 *
 * Estructura (MVC):
 *   src/model/       estado del rover, ordenes y cinematica (sin hardware)
 *   src/view/        telemetria: el estado tal como lo ven los paneles
 *   src/controller/  reglas de mando y failsafe
 *   src/hal/         drivers de motores, servos y paso a paso
 *   src/transport/   cable y Wi-Fi: bytes <-> ordenes / telemetria
 *
 * Ajustes del IDE: ver README.md de esta carpeta.
 */

#include "src/config/RoverConfig.h"
#include "src/model/RoverModel.h"
#include "src/hal/MotorDriver.h"
#include "src/hal/ServoDriver.h"
#include "src/hal/StepperDriver.h"
#include "src/controller/CommandController.h"
#include "src/controller/SafetyController.h"
#include "src/transport/SerialLink.h"
#include "src/transport/WebLink.h"
#include "src/util/Log.h"

static RoverModel        model;
static MotorDriver       motors;
static ServoDriver       servos;
static StepperDriver     stepper;
static CommandController controller(model, motors, servos, stepper);
static SafetyController  safety(controller);
static SerialLink        serialLink(controller);
static WebLink           webLink(controller);

static void haltOnError(const char *what)
{
    // Sin motores o sin controlador no hay forma segura de seguir.
    Log::warn("ROVER", "Fallo critico al iniciar %s. Rover detenido.", what);
    while (true) delay(1000);
}

void setup()
{
    SerialLink::openPort();

    Log::info("ROVER", "Rover 6x6 - ESP32-S3 arrancando (cable: %s, web: %s)",
              cfg::kLinkSerial ? "si" : "no", cfg::kLinkWeb ? "si" : "no");

    // El orden importa: los actuadores tienen que estar en estado seguro antes
    // de que los enlaces empiecen a aceptar ordenes.
    if (!motors.begin())     haltOnError("los motores");
    if (!stepper.begin())    haltOnError("el paso a paso");
    servos.begin();          // no fatal: el rover rueda aunque falte la placa de servos
    if (!controller.begin()) haltOnError("el controlador");

    controller.centerServos();

    if (cfg::kLinkSerial && !serialLink.begin()) haltOnError("el enlace por cable");

    /*
     * El addon web se levanta despues del enlace por cable y no es fatal:
     * si el Wi-Fi falla, el rover tiene que seguir gobernable por USB.
     */
    if (cfg::kLinkWeb && !webLink.begin()) {
        Log::warn("ROVER", "El addon web no arranco. El enlace por cable sigue operativo.");
    }

    // Un unico failsafe para todos los transportes.
    if (!safety.begin()) haltOnError("el failsafe");

    Log::info("ROVER", "Subsistemas en linea.");

    // A partir de aqui el log comparte el cable con la telemetria: solo avisos.
    if (cfg::kLinkSerial) Log::quiet();
}

void loop()
{
    // Todo corre en tareas propias. La tarea de loop() se elimina para no
    // ocupar su pila sin hacer nada.
    vTaskDelete(nullptr);
}
