#include "Kinematics.h"

#include "../config/RoverConfig.h"

namespace kinematics {

float tractionSpeed(float linear, float angular)
{
    (void)angular;                               // el giro lo hacen los servos
    if (linear >  100.0f) return  100.0f;
    if (linear < -100.0f) return -100.0f;
    return linear;
}

float steeringAngle(float angular)
{
    const float lo = cfg::kSteerCenterDeg - cfg::kSteerThrowDeg;
    const float hi = cfg::kSteerCenterDeg + cfg::kSteerThrowDeg;

    float steer = cfg::kSteerCenterDeg + (angular / 100.0f) * cfg::kSteerThrowDeg;
    if (steer < lo) steer = lo;
    if (steer > hi) steer = hi;
    return steer;
}

}  // namespace kinematics
