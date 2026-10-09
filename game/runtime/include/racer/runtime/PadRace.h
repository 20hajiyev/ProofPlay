#pragma once
#include "racer/runtime/RaceSession.h"

namespace racer::runtime
{
    // Temporary M2 race layout on the greybox tuning ground until the Harbor tracks exist:
    // a 1.35 km rounded rectangle on open asphalt, away from the ramp, curbs and wall.
    TrackRoute MakePadRoute();

    // Two-column grid behind the line and pickup rows every 150 m cycling all eight abilities.
    RaceSetup MakePadRaceSetup(const std::vector<VehicleDefinition>& cars, int laps);
}
