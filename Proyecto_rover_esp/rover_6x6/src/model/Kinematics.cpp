#include "Kinematics.h"

#include <math.h>

namespace kinematics {

float tractionSpeed(float linear, float angular)
{
    (void)angular;                               // el giro lo hacen los servos
    if (isnan(linear)) return 0.0f;
    if (linear >  100.0f) return  100.0f;
    if (linear < -100.0f) return -100.0f;
    return linear;
}

void steeringAngles(float angular, float out[cfg::kSteeringServosInDrive])
{
    if (isnan(angular)) angular = 0.0f;
    if (angular >  100.0f) angular =  100.0f;
    if (angular < -100.0f) angular = -100.0f;
    const float delta = (angular / 100.0f) * cfg::kSteerThrowDeg;

    for (int ch = 0; ch < cfg::kSteeringServosInDrive; ch++) {
        float d = (ch < 2) ? delta : -delta;     // 0,1 delanteros; 2,3 traseros
        d *= cfg::kServoInvert[ch];
        // El recorrido se acota ANTES de sumar el trim: si se acotara despues,
        // el trim se perderia justo a tope de giro, donde mas se nota.
        if (d >  cfg::kSteerThrowDeg) d =  cfg::kSteerThrowDeg;
        if (d < -cfg::kSteerThrowDeg) d = -cfg::kSteerThrowDeg;
        out[ch] = cfg::kSteerCenterDeg + d + cfg::kServoTrimDeg[ch];
    }
}

}  // namespace kinematics
