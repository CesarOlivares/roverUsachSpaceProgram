#include "Kinematics.h"

#include <math.h>
#include "../config/RoverConfig.h"

namespace kinematics {

SideSpeeds mixDifferential(float linear, float angular)
{
    float left  = linear + angular;
    float right = linear - angular;

    float peak = fabsf(left) > fabsf(right) ? fabsf(left) : fabsf(right);
    if (peak > 100.0f) {
        left  = (left  / peak) * 100.0f;
        right = (right / peak) * 100.0f;
    }
    return { left, right };
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
