#include "CommandController.h"

#include <math.h>
#include "esp_timer.h"
#include "esp_system.h"
#include "../model/Kinematics.h"
#include "../util/Log.h"

namespace {

// Guarda de cerrojo para no olvidar nunca un xSemaphoreGive en un return.
class Lock {
public:
    explicit Lock(SemaphoreHandle_t m) : m_(m) { xSemaphoreTake(m_, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(m_); }
    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;
private:
    SemaphoreHandle_t m_;
};

}  // namespace

CommandController::CommandController(RoverModel &model, MotorDriver &motors,
                                     ServoDriver &servos, StepperDriver &stepper)
    : model_(model), motors_(motors), servos_(servos), stepper_(stepper)
{
}

bool CommandController::begin()
{
    if (!mtx_) mtx_ = xSemaphoreCreateMutex();
    return mtx_ != nullptr;
}

/* ------------------------------------------------------------------------ */
/* Acciones sobre el hardware (con el mutex tomado)                          */
/* ------------------------------------------------------------------------ */

void CommandController::safeStopLocked()
{
    motors_.stopAll();
    for (int i = 0; i < cfg::kMotorCount; i++) model_.setMotor(i, 0.0f);

    stepper_.stop();

    for (int ch = 0; ch < cfg::kServoCount; ch++) setServoLocked(ch, cfg::kSteerCenterDeg);
}

void CommandController::setServoLocked(int ch, float angle)
{
    float applied = servos_.setAngle(ch, angle);
    if (!isnan(applied)) model_.setServo(ch, applied);
}

void CommandController::driveLocked(float linear, float angular)
{
    kinematics::SideSpeeds s = kinematics::mixDifferential(linear, angular);

    // Indices pares = lado izquierdo (M1, M3, M5), impares = derecho.
    for (int i = 0; i < cfg::kMotorCount; i++) {
        float applied = motors_.apply(i, (i % 2 == 0) ? s.left : s.right);
        if (!isnan(applied)) model_.setMotor(i, applied);
    }

    // ServoDriver filtra los angulos repetidos, asi que en marcha recta esto
    // no genera trafico I2C. El servo en suspension no sigue la direccion.
    float steer = kinematics::steeringAngle(angular);
    for (int ch = 0; ch < cfg::kSteeringServosInDrive; ch++) setServoLocked(ch, steer);
}

/* ------------------------------------------------------------------------ */
/* Dispatcher                                                                */
/* ------------------------------------------------------------------------ */

void CommandController::reportInvalidFrame()
{
    Lock lock(mtx_);
    model_.noteInvalidFrame();
}

void CommandController::handle(const Command &cmd)
{
    Lock lock(mtx_);

    model_.notePacket(cmd.client, cmd.hasSeq, cmd.seq, esp_timer_get_time());

    switch (cmd.type) {
    case CommandType::Heartbeat:
        return;                                  // ya renovo el enlace

    // Parar es universal: no pasa por el arbitraje del mando y ademas lo libera.
    case CommandType::Disarm:
    case CommandType::Stop:
        model_.setArmed(false);
        safeStopLocked();
        model_.releaseControl();
        Log::info("CTRL", "ROVER DESARMADO.");
        return;

    case CommandType::Unknown:
        Log::warn("CTRL", "Accion desconocida.");
        return;

    default:
        break;
    }

    // A partir de aqui todo son ordenes que mueven hardware: exigen el mando.
    // Si otro operador lo tiene se descarta en silencio: a 20 Hz, avisar por
    // log inundaria la consola.
    if (!model_.claimControl(cmd.client)) return;
    if (!cmd.argsOk) return;

    switch (cmd.type) {
    case CommandType::Arm:
        model_.setArmed(true);
        Log::info("CTRL", "ROVER ARMADO.");
        break;

    case CommandType::Drive:
        if (!model_.armed()) return;             // se ignora en silencio si esta desarmado
        driveLocked(cmd.linear, cmd.angular);
        break;

    case CommandType::TestMotor: {
        float applied = motors_.apply(cmd.motor, cmd.speed);
        if (!isnan(applied)) model_.setMotor(cmd.motor, applied);
        break;
    }

    case CommandType::ServoSet:
        setServoLocked(cmd.channel, cmd.angle);
        break;

    case CommandType::StepperJog: {
        int32_t steps = cmd.steps;
        if (steps >  cfg::kStepperMaxJog) steps =  cfg::kStepperMaxJog;
        if (steps < -cfg::kStepperMaxJog) steps = -cfg::kStepperMaxJog;
        stepper_.jog(steps, cmd.stepHz);
        break;
    }

    default:
        break;
    }
}

void CommandController::centerServos()
{
    Lock lock(mtx_);
    for (int ch = 0; ch < cfg::kServoCount; ch++) setServoLocked(ch, cfg::kSteerCenterDeg);
}

/* ------------------------------------------------------------------------ */
/* Telemetria y failsafe                                                     */
/* ------------------------------------------------------------------------ */

RoverSnapshot CommandController::snapshot()
{
    RoverSnapshot s = {};
    {
        Lock lock(mtx_);
        model_.fillSnapshot(s);
    }
    s.uptimeS       = (uint32_t)(esp_timer_get_time() / 1000000LL);
    s.stepperPos    = stepper_.position();
    s.stepperMoving = stepper_.isMoving();
    s.freeHeap      = (uint32_t)esp_get_free_heap_size();
    s.clients       = 0;   // la base pone los suyos al reconstruir la telemetria
    s.radioLq       = -1;  // la calidad del enlace la mide la base, no el rover
    return s;
}

bool CommandController::controlLinkLost(int64_t timeoutUs)
{
    /*
     * Vigila siempre que haya algo que proteger, no solo con el rover armado:
     * un TEST_MOTOR desde el panel de diagnostico mueve las ruedas con el rover
     * desarmado, y si el cliente se cierra a media prueba nadie mandaria el
     * TEST_MOTOR de parada. Tambien se vigila al dueno del mando aunque este
     * quieto: si se fue sin desarmar, otro operador debe poder tomarlo.
     */
    bool moving = stepper_.isMoving();

    Lock lock(mtx_);
    bool guarded = model_.armed() || model_.anyMotorActive() || moving || model_.hasOwner();
    return guarded && model_.controlSilenceUs(esp_timer_get_time()) > timeoutUs;
}

void CommandController::failsafeStop()
{
    Lock lock(mtx_);
    model_.setArmed(false);
    safeStopLocked();
    model_.releaseControl();
    // Evita relanzar el aviso en bucle mientras el enlace siga caido.
    model_.resetControlTimer(esp_timer_get_time());
}

void CommandController::resetLinkTimer()
{
    Lock lock(mtx_);
    model_.resetControlTimer(esp_timer_get_time());
}
