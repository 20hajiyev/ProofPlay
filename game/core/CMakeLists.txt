add_library(racer_core STATIC
    src/SimClock.cpp
    src/Vehicle.cpp
    src/StabilityAssist.cpp
    src/Recovery.cpp
    src/Race.cpp
    src/AI.cpp
    src/Combat.cpp
    src/CombatTuning.cpp
    src/AICombat.cpp
    src/Track.cpp
    src/Career.cpp
    src/Event.cpp
    src/Localization.cpp
    src/Audio.cpp
    src/BodyMotion.cpp
    src/Customization.cpp
    src/Performance.cpp
    src/Weather.cpp
    src/Damage.cpp
    src/Contact.cpp
    src/CameraFeel.cpp
)
target_include_directories(racer_core PUBLIC include)
target_link_libraries(racer_core PRIVATE nlohmann_json)
if(MSVC)
    target_compile_options(racer_core PRIVATE /W4 /WX)
endif()
