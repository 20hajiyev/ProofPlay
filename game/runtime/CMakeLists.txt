add_library(racer_runtime STATIC
    src/PhysicsStepper.cpp
    src/HandlingTrack.cpp
    src/VehicleRuntime.cpp
    src/RaceSession.cpp
    src/PadRace.cpp
    src/TrackBuilder.cpp
)
target_include_directories(racer_runtime PUBLIC include)
target_link_libraries(racer_runtime PUBLIC racer_core WickedEngine)
# Wicked builds Jolt with this define at directory scope only; it changes Jolt class layouts,
# so every TU that includes Jolt headers must match (verified at runtime by VerifyJoltVersionID).
target_compile_definitions(racer_runtime PUBLIC JPH_DEBUG_RENDERER)
